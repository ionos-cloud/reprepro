/*  This file is part of "reprepro"
 *  Copyright (C) 2005,2007 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>

#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "database.h"
#include "exports.h"
#include "configparser.h"
#include "filecntl.h"

extern int verbose;

static const char *exportdescription(const struct exportmode *mode,char *buffer,size_t buffersize) {
	char *result = buffer;
	enum indexcompression ic;
	static const char* compression_names[ic_count] = {
		"uncompressed"
		,"gzipped"
#ifdef HAVE_LIBBZ2
		,"bzip2ed"
#endif
	};
	bool needcomma = false;

	assert( buffersize > 50 );
	*buffer++ = ' '; buffersize--;
	*buffer++ = '('; buffersize--;
	for( ic = ic_first ; ic < ic_count ; ic++ ) {
		if( (mode->compressions & IC_FLAG(ic)) != 0 ) {
			size_t l = strlen(compression_names[ic]);
			assert( buffersize > l+3 );
			if( needcomma ) {
				*buffer++ = ','; buffersize--;
			}
			memcpy(buffer, compression_names[ic], l);
			buffer += l; buffersize -= l;
			needcomma = true;
		}
	}
	if( mode->hook != NULL ) {
		size_t l = strlen(mode->hook);
		if( needcomma ) {
			*buffer++ = ','; buffersize--;
		}
		strcpy(buffer, "script: ");
		buffer += 8; buffersize -= 8;
		if( l > buffersize - 2 ) {
			memcpy(buffer, mode->hook, buffersize-5);
			buffer += (buffersize-5);
			buffersize -= (buffersize-5);
			*buffer++ = '.'; buffersize--;
			*buffer++ = '.'; buffersize--;
			*buffer++ = '.'; buffersize--;
			assert( buffersize >= 2 );
		} else {
			memcpy(buffer, mode->hook, l);
			buffer += l; buffersize -= l;
			assert( buffersize >= 2 );
		}
	}
	assert( buffersize >= 2  );
	*buffer++ = ')'; buffersize--;
	*buffer = '\0';
	return result;
}

retvalue exportmode_init(/*@out@*/struct exportmode *mode, bool uncompressed, /*@null@*/const char *release, const char *indexfile) {
	mode->hook = NULL;
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
	return RET_OK;
}

// TODO: check for scripts in confdir early...
retvalue exportmode_set(struct exportmode *mode, const char *confdir, struct configiterator *iter) {
	retvalue r;
	char *word;

	r = config_getword(iter, &word);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr,
"Error parsing %s, line %u, column %u: Unexpected end of field!\n"
"Filename to use for index files (Packages, Sources, ...) missing.\n",
			config_filename(iter),
			config_markerline(iter), config_markercolumn(iter));
		return RET_ERROR_MISSING;
	}
	assert( word[0] != '\0' );
	free(mode->filename);
	mode->filename = word;

	r = config_getword(iter, &word);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING )
		word = NULL;
	if( r != RET_NOTHING && word[0] != '.' ) {
		assert( word[0] != '\0' );
		free(mode->release);
		mode->release = word;
		r = config_getword(iter, &word);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	if( r == RET_NOTHING ) {
		fprintf(stderr,
"Error parsing %s, line %u, column %u: Unexpected end of field!\n"
"Compression identifiers ('.', '.gz' or '.bz2') missing.\n",
			config_filename(iter),
			config_markerline(iter), config_markercolumn(iter));
		return RET_ERROR;
	}
	if( word[0] != '.' ) {
		fprintf(stderr,
"Error parsing %s, line %u, column %u:\n"
"Compression extension ('.', '.gz' or '.bz2') expected.\n",
			config_filename(iter),
			config_markerline(iter), config_markercolumn(iter));
		free(word);
		return RET_ERROR;
	}
	mode->compressions = 0;
	while( r != RET_NOTHING && word[0] == '.' ) {
		if( word[1] == '\0' )
			mode->compressions |= IC_FLAG(ic_uncompressed);
		else if( word[1] == 'g' && word[2] == 'z' &&
				word[3] == '\0')
			mode->compressions |= IC_FLAG(ic_gzip);
#ifdef HAVE_LIBBZ2
		else if( word[1] == 'b' && word[2] == 'z' && word[3] == '2' &&
				word[4] == '\0')
			mode->compressions |= IC_FLAG(ic_bzip2);
#endif
		else {
			fprintf(stderr,
"Error parsing %s, line %u, column %u:\n"
"Unsupported compression extension '%s'!\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter),
					word);
			free(word);
			return RET_ERROR;
		}
		free(word);
		r = config_getword(iter, &word);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	if( r != RET_NOTHING ) {
		if( word[0] == '/' ) {
			free(mode->hook);
			mode->hook = word;
		} else {
			char *fullfilename = calc_dirconcat(confdir, word);
			free(word);
			if( fullfilename == NULL )
				return RET_ERROR_OOM;
			free(mode->hook);
			mode->hook = fullfilename;
		}
	}
	r = config_getword(iter, &word);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r != RET_NOTHING ) {
		fprintf(stderr,
"Error parsing %s, line %u, column %u:\n"
"Trailing garbage after export hook script!\n",
				config_filename(iter),
				config_markerline(iter),
				config_markercolumn(iter));
		free(word);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue gotfilename(const char *relname, size_t l, struct release *release) {

	if( l > 12 && memcmp(relname+l-12,".tobedeleted",12) == 0) {
		char *filename;

		filename = strndup(relname,l-12);
		if( filename == NULL )
			return RET_ERROR_OOM;
		return release_adddel(release,filename);

	} else if( l > 4 && memcmp(relname+(l-4),".new",4) == 0 ) {
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

static retvalue callexporthook(/*@null@*/const char *hook, const char *relfilename, const char *mode, struct release *release) {
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
		fprintf(stderr, "Error %d creating pipe: %s!\n", e, strerror(e));
		return RET_ERRNO(e);
	}

	f = fork();
	if( f < 0 ) {
		int e = errno;
		(void)close(io[0]);
		(void)close(io[1]);
		fprintf(stderr, "Error %d while forking for exporthook: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}
	if( f == 0 ) {
		char *reltmpfilename;
		int e;

		if( dup2(io[1],3) < 0 ) {
			int e = errno;
			fprintf(stderr, "Error %d dup2'ing fd %d to 3: %s\n",
					e, io[1], strerror(e));
			exit(255);
		}
		/* "Doppelt haelt besser": */
		if( io[0] != 3 )
			(void)close(io[0]);
		if( io[1] != 3 )
			(void)close(io[1]);
		closefrom(4);
		/* backward compatibilty */
		reltmpfilename = calc_addsuffix(relfilename,"new");
		if( reltmpfilename == NULL ) {
			exit(255);
		}
		(void)execl(hook, hook, release_dirofdist(release),
				reltmpfilename, relfilename, mode, NULL);
		e = errno;
		fprintf(stderr, "Error %d while executing '%s': %s\n",
				e, hook, strerror(e));
		exit(255);
	}
	close(io[1]);
	markcloseonexec(io[0]);

	if( verbose > 6 )
		printf("Called %s '%s' '%s.new' '%s' '%s'\n",
			hook,release_dirofdist(release),relfilename,relfilename,mode);
	/* read what comes from the client */
	while( true ) {
		ssize_t r;
		int last,j;

		r = read(io[0],buffer+already,999-already);
		if( r < 0 ) {
			int e = errno;
			fprintf(stderr, "Error %d reading from exporthook: %s!\n",
					e, strerror(e));
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
				int e = (j>0)?(j-1):j;
				retvalue ret;

				while( last < j && xisspace(buffer[last]) )
						last++;
				if( last >= j ) {
					last = next;
					continue;
				}
				while( xisspace(buffer[e]) ) {
					e--;
					assert( e >= last );
				}

				ret = gotfilename(buffer+last,e-last+1,release);
				if( RET_WAS_ERROR(ret) ) {
					(void)close(io[0]);
					return ret;
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
			int e = errno;
			fprintf(stderr,
"Error %d while waiting for hook '%s' to finish: %s\n", e, hook, strerror(e));
			return RET_ERRNO(e);
		}
	} while( c != f );
	if( WIFEXITED(status) ) {
		if( WEXITSTATUS(status) == 0 ) {
			if( verbose > 6 )
				printf("Exporthook successfully returned!\n");
			return RET_OK;
		} else {
			fprintf(stderr,"Exporthook failed with exitcode %d!\n",(int)WEXITSTATUS(status));
			return RET_ERROR;
		}
	} else {
		fprintf(stderr,"Exporthook terminated abnormally. (status is %x)!\n",status);
		return RET_ERROR;
	}
}

retvalue export_target(const char *relativedir, struct table *packages, const struct exportmode *exportmode, struct release *release, bool onlyifmissing, bool snapshot) {
	retvalue r;
	struct filetorelease *file;
	const char *status;
	char *relfilename;
	char buffer[100];
	const char *chunk;
	size_t chunk_len;
	struct cursor *cursor;

	relfilename = calc_dirconcat(relativedir,exportmode->filename);
	if( relfilename == NULL )
		return RET_ERROR_OOM;

	r = release_startfile(release,relfilename,exportmode->compressions,onlyifmissing,&file);
	if( RET_WAS_ERROR(r) ) {
		free(relfilename);
		return r;
	}
	if( RET_IS_OK(r) ) {
		if( release_oldexists(file) ) {
			if( verbose > 5 )
				printf("  replacing '%s/%s'%s\n",
					release_dirofdist(release), relfilename,
					exportdescription(exportmode, buffer, 100));
			status = "change";
		} else {
			if( verbose > 5 )
				printf("  creating '%s/%s'%s\n",
					release_dirofdist(release), relfilename,
					exportdescription(exportmode, buffer, 100));
			status = "new";
		}
		r = table_newglobalcursor(packages, &cursor);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(file);
			free(relfilename);
			return r;
		}
		while( cursor_nexttempdata(packages, cursor, NULL,
					&chunk, &chunk_len) ) {
			if( chunk_len == 0 )
				continue;
			(void)release_writedata(file, chunk, chunk_len);
			(void)release_writestring(file, "\n");
			if( chunk[chunk_len-1] != '\n' )
				(void)release_writestring(file, "\n");
		}
		r = cursor_close(packages, cursor);
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
		if( verbose > 9 )
			printf("  keeping old '%s/%s'%s\n",
				release_dirofdist(release), relfilename,
				exportdescription(exportmode, buffer, 100));
		status = "old";
	}
	if( !snapshot )
		r = callexporthook(exportmode->hook,
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
