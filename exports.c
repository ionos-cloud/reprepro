/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include <config.h>

#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>

#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "chunks.h"
#include "packages.h"
#include "exports.h"
#include "md5sum.h"
#include "copyfile.h"

extern int verbose;

static retvalue printout(void *data,UNUSED(const char *package),const char *chunk) {
	struct filetorelease *file = data;
	size_t l;

	l = strlen(chunk);
	if( l == 0 )
		return RET_NOTHING;
	(void)release_writedata(file,chunk,l);
	(void)release_writestring(file,"\n");
	if( chunk[l-1] != '\n' )
		(void)release_writestring(file,"\n");
	return RET_OK;
}

retvalue exportmode_init(/*@out@*/struct exportmode *mode,bool_t uncompressed,/*@null@*/const char *release,const char *indexfile,/*@null@*//*@only@*/char *options) {
	mode->hook = NULL;
	if( options == NULL ) {
		mode->compressions = IC_FLAG(ic_gzip) | (uncompressed?IC_FLAG(ic_uncompressed):0);
		mode->filename = strdup(indexfile);
		if( mode->filename == NULL )
			return RET_ERROR_OOM;
		if( release == NULL )
			mode->release = NULL;
		else {
			mode->release = strdup(release);
			if( mode->release == NULL )
				return RET_ERROR_OOM;
		}
	} else {
		const char *b;
		b = options;
		while( *b != '\0' && xisspace(*b) )
			b++;
		if( *b != '\0' && *b != '.' ) {
			const char *e = b;
			while( *e != '\0' && !xisspace(*e) )
				e++;
			mode->filename = strndup(b,e-b);
			b = e;
		} else {
			mode->filename = strdup(indexfile);
		}
		if( mode->filename == NULL )
			return RET_ERROR_OOM;
		while( *b != '\0' && xisspace(*b) )
			b++;
		if( *b != '\0' && *b != '.' ) {
			const char *e = b;
			while( *e != '\0' && !xisspace(*e) )
				e++;
			mode->release = strndup(b,e-b);
			if( mode->release == NULL )
				return RET_ERROR_OOM;
			b = e;
		} else {
			mode->release = NULL;
		}
		while( *b != '\0' && xisspace(*b) )
			b++;
		if( *b != '.' ) {
			if( *b == '\0' )
				fprintf(stderr,"Expecting '.' or '.gz' in '%s'!\n",options);
			else
				fprintf(stderr,"Third argument is still not '.' nor '.gz' in '%s'!\n",options);
			free(options);
			return RET_ERROR;
		}
		mode->compressions = 0;
		while( *b == '.' ) {
			const char *e = b;
			while( *e != '\0' && !xisspace(*e) )
				e++;
			if( xisspace(b[1]) || b[1] == '\0' )
				mode->compressions |= IC_FLAG(ic_uncompressed);
			else if( b[1] == 'g' && b[2] == 'z' &&
					(xisspace(b[3]) || b[3] == '\0'))
				mode->compressions |= IC_FLAG(ic_gzip);
			else {
				fprintf(stderr,"Unsupported extension '.%c'... in '%s'!\n",b[1],options);
				free(options);
				return RET_ERROR;
			}
			b = e;
			while( *b != '\0' && xisspace(*b) )
				b++;
		}
		if( *b != '\0' ) {
			const char *e = b;
			while( *e != '\0' && !xisspace(*e) )
				e++;
			mode->hook = strndup(b,e-b);
			if( mode->hook == NULL ) {
				free(options);
				return RET_ERROR_OOM;
			}
			b = e;
		}
		while( *b != '\0' && xisspace(*b) )
			b++;
		if( *b != '\0' ) {
			fprintf(stderr,"More than one hook specified(perhaps you have spaces in them?) in '%s'!\n",options);
			free(options);
			return RET_ERROR;
		}
	}
	free(options);
	return RET_OK;
}

static retvalue gotfilename(const char *relname, size_t l, struct release *release) {

	if( l > 12 && memcmp(relname+l-12,".tobedeleted",12) == 0) {
		char *filename;

		filename = strndup(relname,l-12);
		if( filename == NULL )
			return RET_ERROR_OOM;
		return release_adddel(release,filename);

	} if( l > 4 || strcmp(relname+(l-4),".new") == 0 ) {
		char *filename,*tmpfilename;

		filename = strndup(relname,l-4);
		if( filename == NULL )
			return RET_ERROR_OOM;
		tmpfilename = strndup(relname,l);
		if( tmpfilename == NULL ) {
			free(filename);
			return RET_ERROR_OOM;
		}
		return release_addnew(release,tmpfilename,filename);

	} else {
		char *filename;

		filename = strndup(relname,l);
		if( filename == NULL )
			return RET_ERROR_OOM;
		return release_addold(release,filename);
	}
}

static retvalue callexporthook(const char *confdir,/*@null@*/const char *hook, const char *relfilename, const char *mode, struct release *release) {
	pid_t f,c;
	int status; 
	int io[2];
	char buffer[1000];
	int already = 0;

	if( hook == NULL )
		return RET_NOTHING;

	status = pipe(io);
	if( status < 0 ) {
		int e = errno;
		fprintf(stderr,"Error creating pipe: %d=%m!\n",e);
		return RET_ERRNO(e);
	}

	f = fork();
	if( f < 0 ) {
		int err = errno;
		(void)close(io[0]);
		(void)close(io[1]);
		fprintf(stderr,"Error while forking for exporthook: %d=%m\n",err);
		return RET_ERRNO(err);
	}
	if( f == 0 ) {
		long maxopen;
		char *reltmpfilename;

		if( dup2(io[1],3) < 0 ) {
			fprintf(stderr,"Error dup2'ing fd %d to 3: %d=%m\n",
					io[1],errno);
			exit(255);
		}
		/* Try to close all open fd but 0,1,2,3 */
		maxopen = sysconf(_SC_OPEN_MAX);
		if( maxopen > 0 ) {
			int fd;
			for( fd = 4 ; fd < maxopen ; fd++ )
				(void)close(fd);
		} else { // otherweise we have to hope...
			if( io[0] != 3 )
				(void)close(io[0]);
			if( io[1] != 3 )
				(void)close(io[1]);
		}
		/* backward compatibilty */
		reltmpfilename = calc_addsuffix(relfilename,"new");
		if( reltmpfilename == NULL ) {
			exit(255);
		}
		if( hook[0] == '/' )
			(void)execl(hook,hook,release_dirofdist(release),reltmpfilename,relfilename,mode,NULL);
		else {
			char *fullfilename = calc_dirconcat(confdir,hook);
			if( fullfilename == NULL ) {
				fprintf(stderr,"Out of Memory!\n");
				exit(255);

			}
			(void)execl(fullfilename,fullfilename,release_dirofdist(release),reltmpfilename,relfilename,mode,NULL);
		}
		fprintf(stderr,"Error while executing '%s': %d=%m\n",hook,errno);
		exit(255);
	}
	close(io[1]);
	
	if( verbose > 5 )
		fprintf(stderr,"Called %s '%s' '%s.new' '%s' '%s'\n",
			hook,release_dirofdist(release),relfilename,relfilename,mode);
	/* read what comes from the client */
	while( TRUE ) {
		ssize_t r;
		int last,j;

		r = read(io[0],buffer+already,999-already);
		if( r < 0 ) {
			int e = errno;
			fprintf(stderr,"Error reading from exporthook: %d=%m!\n",e);
			break;
		}

		already += r;
		if( r == 0 ) {
			buffer[already] = '\0';
			already++;
		}
		last = 0;
		for( j = 0 ; j < already ; j++ ) {
			if( buffer[j] == '\n' || buffer[j] == '\0' ) {
				int next = j+1;

				while( last<j && xisspace(buffer[last]) )
						last++;
				while( j>last && xisspace(buffer[j]) )
					j--;
				// This makes on character long files impossible,
				// but who needs them?
				if( last < j ) {
					retvalue ret;

					ret = gotfilename(buffer+last,j-last+1,release);
					if( RET_WAS_ERROR(ret) ) {
						(void)close(io[0]);
						return ret;
					}
				}
				last = next;
			}
		}
		if( last > 0 ) {
			if( already > last )
				memmove(buffer,buffer+last,already-last);
			already -= last;
		}
		if( r == 0 )
			break;
	}
	(void)close(io[0]);
	do {
		c = waitpid(f,&status,WUNTRACED);
		if( c < 0 ) {
			int err = errno;
			fprintf(stderr,"Error while waiting for hook '%s' to finish: %d=%m\n",hook,err);
			return RET_ERRNO(err);
		}
	} while( c != f );
	if( WIFEXITED(status) ) {
		if( WEXITSTATUS(status) == 0 ) {
			if( verbose > 5 )
				fprintf(stderr,"Exporthook successfully returned!\n");
			return RET_OK;
		} else {
			fprintf(stderr,"Exporthook failed with exitcode %d!\n",(int)WEXITSTATUS(status));
			return RET_ERROR;
		}
	} else {
		fprintf(stderr,"Exporthook terminated abnormaly. (status is %x)!\n",status);
		return RET_ERROR;
	}
}

retvalue export_target(const char *confdir,const char *relativedir,packagesdb packages,const struct exportmode *exportmode,struct release *release, bool_t onlyifmissing) {
	retvalue r;
	struct filetorelease *file;
	const char *status;
	char *relfilename;

	relfilename = calc_dirconcat(relativedir,exportmode->filename);
	if( relfilename == NULL )
		return RET_ERROR_OOM;

	r = release_startfile(release,relfilename,exportmode->compressions,onlyifmissing,&file);
	if( RET_WAS_ERROR(r) ) {
		free(relfilename);
		return r;
	}
	if( RET_IS_OK(r) ) {
		if( release_oldexists(file) )
			status = "change";
		else
			status = "new";
		r = packages_foreach(packages,printout,file,0);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(file);
			free(relfilename);
			return r;
		}
		r = release_finishfile(release,file);
		if( RET_WAS_ERROR(r) ) {
			free(relfilename);
			return r;
		}
	} else {
		status = "old";
	}
	r = callexporthook(confdir,
			exportmode->hook,
					relfilename,
					status,
					release);
	free(relfilename);
	if( RET_WAS_ERROR(r) )
		return r;
	return RET_OK;
}

void exportmode_done(struct exportmode *mode) {
	assert( mode != NULL);
	free(mode->filename);
	free(mode->hook);
	free(mode->release);
}
