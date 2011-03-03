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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
	FILE *pf = data;
	size_t l;

	l = strlen(chunk);
	if( fwrite(chunk,l,1,pf) != 1 || fwrite("\n",1,1,pf) != 1 )
		return RET_ERROR;
	else {
		if( chunk[l-1] != '\n' )
			if( fwrite("\n",1,1,pf) != 1 )
				return RET_ERROR;
		return RET_OK;
	}
}

static retvalue zprintout(void *data,UNUSED(const char *package),const char *chunk) {
	gzFile pf = data;
	size_t l;

	l = strlen(chunk);
	if( gzwrite(pf,(const voidp)chunk,l) != l || gzwrite(pf,"\n",1) != 1 )
		return RET_ERROR;
	else {
		if( chunk[l-1] != '\n' )
			if( gzwrite(pf,"\n",1) != 1 )
				return RET_ERROR;
		return RET_OK;
	}
}

/* print the database to a "Packages" or "Sources" file */
static retvalue packagesdb_printout(packagesdb packagesdb,const char *filename) {
	retvalue ret;
	int r;
	FILE *pf;

	pf = fopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		return RET_ERRNO(errno);
	}
	ret = packages_foreach(packagesdb,printout,pf,0);
	r = fclose(pf);
	if( r != 0 )
		RET_ENDUPDATE(ret,RET_ERRNO(errno));
	/* Writing an empty file is also something done */
	if( ret == RET_NOTHING )
		return RET_OK;
	return ret;
}

/* print the database to a "Packages.gz" or "Sources.gz" file */
static retvalue packagesdb_zprintout(packagesdb packagesdb,const char *filename) {
	retvalue ret;
	int r;
	gzFile pf;

	pf = gzopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		/* if errno is zero, it's a memory error: */
		return RET_ERRNO(errno);
	}
	ret = packages_foreach(packagesdb,zprintout,pf,0);
	r = gzclose(pf);
	if( r < 0 )
		RET_ENDUPDATE(ret,RET_ZERRNO(r));
	/* Writing an empty file is also something done */
	if( ret == RET_NOTHING )
		return RET_OK;
	return ret;
}


static retvalue export_writepackages(packagesdb packagesdb,const char *filename,indexcompression compression) {

	if( verbose > 4 ) {
		fprintf(stderr,"  writing to '%s'...\n",filename);
	}

	(void)unlink(filename);
	switch( compression ) {
		case ic_uncompressed:
			return packagesdb_printout(packagesdb,filename);
		case ic_gzip:
			return packagesdb_zprintout(packagesdb,filename);
	}
	assert( compression == 0 && compression != 0 );
	return RET_ERROR;
}

/*@null@*/
static char *comprconcat(const char *str2,const char *str3,indexcompression compression) {

	switch( compression ) {
		case ic_uncompressed:
			return mprintf("%s/%s",str2,str3);
		case ic_gzip:
			return mprintf("%s/%s.gz",str2,str3);
	}
	assert( compression == 0 && compression != 0 );
	return NULL;
}


retvalue exportmode_init(/*@out@*/struct exportmode *mode,bool_t uncompressed,/*@null@*/const char *release,const char *indexfile,/*@null@*//*@only@*/char *options) {
	mode->hook = NULL;
	if( options == NULL ) {
		mode->compressions[ic_uncompressed] = uncompressed;
		mode->compressions[ic_gzip] = TRUE;
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
		while( *b != '\0' && isspace(*b) )
			b++;
		if( *b != '\0' && *b != '.' ) {
			const char *e = b;
			while( *e != '\0' && !isspace(*e) )
				e++;
			mode->filename = strndup(b,e-b);
			b = e;
		} else {
			mode->filename = strdup(indexfile);
		}
		if( mode->filename == NULL )
			return RET_ERROR_OOM;
		while( *b != '\0' && isspace(*b) )
			b++;
		if( *b != '\0' && *b != '.' ) {
			const char *e = b;
			while( *e != '\0' && !isspace(*e) )
				e++;
			mode->release = strndup(b,e-b);
			if( mode->release == NULL )
				return RET_ERROR_OOM;
			b = e;
		} else {
			mode->release = NULL;
		}
		while( *b != '\0' && isspace(*b) )
			b++;
		if( *b != '.' ) {
			if( *b == '\0' )
				fprintf(stderr,"Expecting '.' or '.gz' in '%s'!\n",options);
			else
				fprintf(stderr,"Third argument is still not '.' nor '.gz' in '%s'!\n",options);
			free(options);
			return RET_ERROR;
		}
		mode->compressions[ic_uncompressed] = FALSE;
		mode->compressions[ic_gzip] = FALSE;
		while( *b == '.' ) {
			const char *e = b;
			while( *e != '\0' && !isspace(*e) )
				e++;
			if( isspace(b[1]) || b[1] == '\0' )
				mode->compressions[ic_uncompressed] = TRUE;
			else if( b[1] == 'g' && b[2] == 'z' &&
					(isspace(b[3]) || b[3] == '\0'))
				mode->compressions[ic_gzip] = TRUE;
			else {
				fprintf(stderr,"Unsupported extension '.%c'... in '%s'!\n",b[1],options);
				free(options);
				return RET_ERROR;
			}
			b = e;
			while( *b != '\0' && isspace(*b) )
				b++;
		}
		if( *b != '\0' ) {
			const char *e = b;
			while( *e != '\0' && !isspace(*e) )
				e++;
			mode->hook = strndup(b,e-b);
			if( mode->hook == NULL ) {
				free(options);
				return RET_ERROR_OOM;
			}
			b = e;
		}
		while( *b != '\0' && isspace(*b) )
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

static retvalue callexporthook(const char *confdir,/*@null@*/const char *hook, const char *dirofdist, const char *reltmpfilename, const char *relfilename, const char *mode, struct strlist *releasedfiles) {
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
		if( hook[0] == '/' )
			(void)execl(hook,hook,dirofdist,reltmpfilename,relfilename,mode,NULL);
		else {
			char *fullfilename = calc_dirconcat(confdir,hook);
			if( fullfilename == NULL ) {
				fprintf(stderr,"Out of Memory!\n");
				exit(255);

			}
			(void)execl(fullfilename,fullfilename,dirofdist,reltmpfilename,relfilename,mode,NULL);
		}
		fprintf(stderr,"Error while executing '%s': %d=%m\n",hook,errno);
		exit(255);
	}
	close(io[1]);
	
	if( verbose > 5 )
		fprintf(stderr,"Called %s '%s' '%s' '%s' '%s'\n",
			hook,dirofdist,reltmpfilename,relfilename,mode);
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

				while( last<j && isspace(buffer[last]) )
						last++;
				while( j>last && isspace(buffer[j]) )
					j--;
				// This makes on character long files impossible,
				// but who needs them?
				if( last < j ) {
					char *item;
					retvalue ret;

					item = strndup(buffer+last,j-last+1);
					if( item == NULL ) {
						(void)close(io[0]);
						return RET_ERROR_OOM;
					}
					ret = strlist_add(releasedfiles,item);
					if( RET_WAS_ERROR(ret) ) {
						(void)close(io[0]);
						return RET_ERROR_OOM;
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

retvalue export_target(const char *confdir,const char *dirofdist,const char *relativedir,packagesdb packages,const struct exportmode *exportmode,struct strlist *releasedfiles, bool_t onlymissing, int force) {
	indexcompression compression;
	retvalue result,r;

	result = RET_NOTHING;

	//TODO: rewrite to .new managment and releasedfile file

	for( compression = 0 ; compression <= ic_max ; compression++) {
		if( exportmode->compressions[compression] ) {
			char *relfilename,*reltmpfilename,*fullfilename;
			bool_t alreadyexists;

			relfilename = comprconcat(relativedir,
					exportmode->filename,compression);
			if( relfilename == NULL )
				return RET_ERROR_OOM;
			fullfilename = calc_dirconcat(dirofdist,relfilename);
			if( fullfilename == NULL ) {
				free(relfilename);
				return RET_ERROR_OOM;
			}

			alreadyexists = isregularfile(fullfilename);
			free(fullfilename);

			reltmpfilename = calc_addsuffix(relfilename,"new");
			if( reltmpfilename == NULL ) {
				free(relfilename);
				return RET_ERROR_OOM;
			}
			fullfilename = calc_dirconcat(dirofdist,reltmpfilename);
			if( fullfilename == NULL ) {
				free(relfilename);
				free(reltmpfilename);
				return RET_ERROR_OOM;
			}
			(void)unlink(fullfilename);

			if( alreadyexists && onlymissing ) {
				free(fullfilename);
				r = callexporthook(confdir,exportmode->hook,
						dirofdist,
						reltmpfilename,relfilename,
						"old",
						releasedfiles);
				free(reltmpfilename);
				RET_UPDATE(result,r);
				if( !force && RET_WAS_ERROR(r) ) {
					free(relfilename);
					return r;
				}
				r = strlist_add(releasedfiles,relfilename);
				if( RET_WAS_ERROR(r) ) {
					return r;
				}
				continue;
			}
			
			r = export_writepackages(packages,
					fullfilename,compression);
			free(fullfilename);
			RET_UPDATE(result,r);
			if( !force && RET_WAS_ERROR(r) ) {
				free(reltmpfilename);
				free(relfilename);
				return r;
			}

			// TODO: allow multiple hooks?
			r = callexporthook(confdir,
					exportmode->hook,dirofdist,
					reltmpfilename,relfilename,
					alreadyexists?"change":"new",
					releasedfiles);
			free(relfilename);
			RET_UPDATE(result,r);
			if( !force && RET_WAS_ERROR(r) ) {
				free(reltmpfilename);
				return r;
			}

			r = strlist_add(releasedfiles,reltmpfilename);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return result;
}

void exportmode_done(struct exportmode *mode) {
	assert( mode != NULL);
	free(mode->filename);
	free(mode->hook);
}

retvalue export_checksums(const char *dirofdist,FILE *f,struct strlist *releasedfiles, int force) {
	retvalue result,r;
	int i,e;

	result = RET_NOTHING;

#define checkwritten if( e < 0 ) { \
		e = errno; \
		fprintf(stderr,"Error writing in Release.new: %d=%m!\n",e); \
		return RET_ERRNO(e); \
	}


	e = fprintf(f,"MD5Sum:\n");
	checkwritten;

	for( i = 0 ; i < releasedfiles->count ; i++ ) {
		const char *relname = releasedfiles->values[i];
		char *fullfilename,*md5sum;
		size_t l;

		l = strlen(relname);
		if( l > 12 && strcmp(relname+(l-12),".tobedeleted") == 0 ) {
			/* deleted files will not show up in a Release file */
			continue;
		}
		fullfilename = calc_dirconcat(dirofdist,relname);
		if( fullfilename == NULL )
			return RET_ERROR_OOM;

		if( !isregularfile(fullfilename) ) {
			fprintf(stderr,"Cannot find (or not regular file): '%s'\n",fullfilename);
			r = RET_ERROR_MISSING;
		} else
			r = md5sum_read(fullfilename,&md5sum);
		if( !RET_IS_OK(r) ) {
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Cannot find %s\n",fullfilename);
				r = RET_ERROR_MISSING;
			}
			free(fullfilename);
			if( force > 0 ) {
				RET_UPDATE(result,r);
				continue;
			} else
				return r;
		}
		free(fullfilename);
		e = fputc(' ',f);
		if( e == 0 ){
			free(md5sum);
			e = -1;
			checkwritten;
			assert(0);
		}
		e = fputs(md5sum,f);
		free(md5sum);
		checkwritten;
		e = fputc(' ',f) - 1;
		checkwritten;
		if( l > 4 && strcmp(relname+(l-4),".new") == 0 ) {
			size_t written;
			written = fwrite(relname,sizeof(char),l-4,f);
			if( written != l-4 ) {
				e = -1;
				checkwritten;
			}
		} else {
			fputs(relname,f);
			checkwritten;
		}
		e = fputc('\n',f) - 1;
		checkwritten;
	}
	return result;
#undef checkwritten
}

retvalue export_finalize(const char *dirofdist,struct strlist *releasedfiles, int force, bool_t issigned) {
	retvalue result,r;
	int i,e;
	bool_t somethingwasdone;
	char *tmpfullfilename,*finalfullfilename;

	result = RET_NOTHING;
	somethingwasdone = FALSE;

	/* after all is written and all is computed, move all the files
	 * at once: */
	for( i = 0 ; i < releasedfiles->count ; i++ ) {
		const char *relname = releasedfiles->values[i];
		size_t l;

		l = strlen(relname);
		if( l > 12 && strcmp(relname+(l-12),".tobedeleted") == 0 ) {
			tmpfullfilename = calc_dirconcatn(dirofdist,relname,l-12);
			if( tmpfullfilename == NULL )
				return RET_ERROR_OOM;
			e = unlink(tmpfullfilename);
			if( e < 0 ) {
				e = errno;
				// TODO: what to do in case of error?
				fprintf(stderr,"Error deleting %s: %m. (Will be ignored)\n",tmpfullfilename);
			}
			free(tmpfullfilename);
			continue;
		}
		if( l <= 4 || strcmp(relname+(l-4),".new") != 0 )
			continue;
		tmpfullfilename = calc_dirconcat(dirofdist,relname);
		if( tmpfullfilename == NULL )
			return RET_ERROR_OOM;
		finalfullfilename = calc_dirconcatn(dirofdist,relname,l-4);
		if( finalfullfilename == NULL ) {
			free(tmpfullfilename);
			return RET_ERROR_OOM;
		}
		e = rename(tmpfullfilename,finalfullfilename);
		if( e < 0 ) {
			e = errno;
			fprintf(stderr,"Error moving %s to %s: %d=%m!",tmpfullfilename,finalfullfilename,e);
			r = RET_ERRNO(e);
			RET_UPDATE(result,r);
			/* if we moved anything yet, do not stop with
			 * later errors, as it is too late already */
			if( force <= 0 && !somethingwasdone ) {
				free(tmpfullfilename);
				free(finalfullfilename);
				return r;
			}
		} else
			somethingwasdone = TRUE;
		free(tmpfullfilename);
		free(finalfullfilename);
	}
	if( issigned ) {
		tmpfullfilename = calc_dirconcat(dirofdist,"Release.gpg.new");
		if( tmpfullfilename == NULL )
			return RET_ERROR_OOM;
		finalfullfilename = calc_dirconcat(dirofdist,"Release.gpg");
		if( finalfullfilename == NULL ) {
			free(tmpfullfilename);
			return RET_ERROR_OOM;
		}
		e = rename(tmpfullfilename,finalfullfilename);
		if( e < 0 ) {
			e = errno;
			fprintf(stderr,"Error moving %s to %s: %d=%m!",tmpfullfilename,finalfullfilename,e);
			r = RET_ERRNO(e);
			RET_UPDATE(result,r);
		}
		free(tmpfullfilename);
		free(finalfullfilename);
	}

	tmpfullfilename = calc_dirconcat(dirofdist,"Release.new");
	if( tmpfullfilename == NULL )
		return RET_ERROR_OOM;
	finalfullfilename = calc_dirconcat(dirofdist,"Release");
	if( finalfullfilename == NULL ) {
		free(tmpfullfilename);
		return RET_ERROR_OOM;
	}
	e = rename(tmpfullfilename,finalfullfilename);
	if( e < 0 ) {
		e = errno;
		fprintf(stderr,"Error moving %s to %s: %d=%m!",tmpfullfilename,finalfullfilename,e);
		r = RET_ERRNO(e);
		RET_UPDATE(result,r);
	}
	free(tmpfullfilename);
	free(finalfullfilename);

	return result;

}
