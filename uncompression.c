/*  This file is part of "reprepro"
 *  Copyright (C) 2008 Bernhard R. Link
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <zlib.h>
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#endif
#include <fcntl.h>

#include "globals.h"
#include "error.h"
#include "uncompression.h"

extern int verbose;

const char * const uncompression_suffix[c_COUNT] = {
	"", ".gz", ".bz2", ".lzma" };


/*@null@*/ char *extern_uncompressors[c_COUNT] = { NULL, NULL, NULL, NULL};


/* we got an pid, check if it is a uncompressor we care for */
retvalue uncompress_checkpid(pid_t pid, int status) {
	// nothing is forked, so nothing to do
	return RET_NOTHING;
}
bool uncompress_running(void) {
	// nothing is forked, so nothing to do
	return false;
}

/* check for existance of external programs */
void uncompressions_check(void) {
	// nothing yet to check
}

static inline retvalue builtin_uncompress(const char *compressed, const char *destination, enum compression compression) {
	struct compressedfile *f;
	char buffer[4096];
	int bytes_read, bytes_written, written;
	int destfd;
	int e;
	retvalue r;

	r = uncompress_open(&f, compressed, compression);
	if( !RET_IS_OK(r) )
		return r;
	destfd = open(destination, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
	if( destfd < 0 ) {
		e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, destination, strerror(e));
		(void)uncompress_close(f);
		return RET_ERRNO(e);
	}
	do {
		bytes_read = uncompress_read(f, buffer, 4096);
		if( bytes_read <= 0 )
			break;

		bytes_written = 0;
		while( bytes_written < bytes_read ) {
			written = write(destfd, buffer + bytes_written,
					bytes_read - bytes_written);
			if( written < 0 ) {
				e = errno;
				fprintf(stderr,
						"Error %d writing to '%s': %s\n", e, destination, strerror(e) );
				close(destfd);
				(void)uncompress_close(f);
				return RET_ERRNO(e);
			}
			bytes_written += written;
		}
	} while( true );
	r = uncompress_close(f);
	if( RET_WAS_ERROR(r) ) {
		(void)close(destfd);
		return r;
	}
	if( close(destfd) != 0 ) {
		e = errno;
		fprintf(stderr, "Error %d writing to '%s': %s!\n",
				e, destination, strerror(e));
		return RET_ERROR;
	}
	return RET_OK;
}

retvalue uncompress_queue_file(const char *compressed, const char *destination, enum compression compression, finishaction *action, void *privdata, bool *done_p) {
	retvalue r;

	if( verbose > 1 ) {
		fprintf(stderr, "Uncompress '%s' into '%s'...\n",
				compressed, destination);
	}


	(void)unlink(destination);

	if( extern_uncompressors[compression] != NULL ) {
		// TODO...
	}
	switch( compression ) {
		case c_gzip:
#ifdef HAVE_LIBBZ2
		case c_bzip2:
#endif
			r = builtin_uncompress(compressed, destination, compression);
			break;
		default:
			assert( compression != compression );
	}
	if( RET_WAS_ERROR(r) ) {
		(void)unlink(destination);
		return r;
	}
	*done_p = true;
	return action(privdata, compressed, true);
}

retvalue uncompress_file(const char *compressed, const char *destination, enum compression compression) {
	retvalue r;

	if( verbose > 1 ) {
		fprintf(stderr, "Uncompress '%s' into '%s'...\n",
				compressed, destination);
	}

	(void)unlink(destination);
	switch( compression ) {
		case c_gzip:
#ifdef HAVE_LIBBZ2
		case c_bzip2:
#endif
			r = builtin_uncompress(compressed, destination, compression);
			break;
		default:
			r = RET_ERROR;
			if( extern_uncompressors[compression] != NULL ) {
				// TODO...
			} else
				assert( compression != compression );
	}
	if( RET_WAS_ERROR(r) ) {
		(void)unlink(destination);
		return r;
	}
	return RET_OK;
}

struct compressedfile {
	char *filename;
	enum compression compression;
	int error;
	union {
		int fd;
		gzFile gz;
		BZFILE *bz;
	};
};

retvalue uncompress_open(/*@out@*/struct compressedfile **file_p, const char *filename, enum compression compression) {
	struct compressedfile *f;
	int e = errno;

	f = calloc(1, sizeof(struct compressedfile));
	if( FAILEDTOALLOC(f) )
		return RET_ERROR_OOM;
	f->filename = strdup(filename);
	if( FAILEDTOALLOC(f->filename) ) {
		free(f);
		return RET_ERROR_OOM;
	}
	f->compression = compression;

	switch( compression ) {
		case c_none:
			f->fd = open(filename, O_RDONLY|O_NOCTTY);
			if( f->fd < 0 ) {
				e = errno;
				free(f->filename);
				free(f);
				// if( e == || e == )
				//	return RET_NOTHING;
				fprintf(stderr, "Error %d opening '%s': %s!\n",
						e, filename, strerror(e));
				return RET_ERRNO(e);
			}
			break;
		case c_gzip:
			f->gz = gzopen(filename, "r");
			if( f->gz == NULL ) {
				// TODO: better error message...
				fprintf(stderr, "Could not read %s\n", filename);
				free(f->filename);
				free(f);
				return RET_ERROR;
			}
			break;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			f->bz = BZ2_bzopen(filename, "r");
			if( f->bz == NULL ) {
				// TODO: better error message...
				fprintf(stderr, "Could not read %s\n",
						filename);
				free(f->filename);
				free(f);
				return RET_ERROR;
			}
			break;
#endif
		default:
			assert( NULL == "internal error in uncompression" );
	}
	*file_p = f;
	return RET_OK;
}

int uncompress_read(struct compressedfile *file, void *buffer, int size) {
	ssize_t s;
	int i;

	switch( file->compression ) {
		case c_none:
			s = read(file->fd, buffer, size);
			if( s < 0 )
				file->error = errno;
			return s;
		case c_gzip:
			i = gzread(file->gz, buffer, size);
			file->error = errno;
			return i;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			i = BZ2_bzread(file->bz, buffer, size);
			file->error = errno;
			return i;
#endif
		default:
			// TODO: external program
			return -1;
	}
}

retvalue uncompress_close(struct compressedfile *file) {
	const char *msg;
	bool error;
	retvalue result = RET_OK;
	int zerror;

	if( file == NULL )
		return RET_OK;

	switch( file->compression ) {
		case c_none:
			if( file->error != 0 ) {
				error = true;
				(void)close(file->fd);
			} else if( close(file->fd) != 0 ) {
				file->error = errno;
				error = true;
			} else
				error = false;
			break;
		case c_gzip:
			msg = gzerror(file->gz, &zerror);
			if( zerror == Z_ERRNO )
				error = true;
			else if( zerror < 0 ) {
				fprintf(stderr,
"Zlib Error %d reading from '%s': %s!\n", zerror, file->filename, msg);
				(void)gzclose(file->gz);
				result = RET_ERROR;
				error = true;
				break;
			} else
				error = false;
			zerror = gzclose(file->gz);
			if( zerror == Z_ERRNO ) {
				error = true;
			} if( zerror < 0 && !error ) {
				result = RET_ERROR;
				fprintf(stderr,
"Zlib Error %d reading from '%s'!\n", zerror, file->filename);
				break;
			}
			break;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			msg = BZ2_bzerror(file->bz, &zerror);
			if( zerror == Z_ERRNO )
				error = true;
			else if( zerror < 0 ) {
				fprintf(stderr,
"libBZ2 Error %d reading from '%s': %s!\n", zerror, file->filename, msg);
				(void)BZ2_bzclose(file->bz);
				result = RET_ERROR;
				error = true;
				break;
			} else
				error = false;
			/* no return? does this mean no checksums? */
			BZ2_bzclose(file->bz);
			break;
#endif
		default:
			// TODO: external helpers...
			assert( NULL == "Internal uncompression error");
			error = true;
	}
	if( error && result == RET_OK ) {
		fprintf(stderr,
"Error %d reading from '%s': %s!\n", file->error, file->filename,
					strerror(file->error));
		result = RET_ERRNO(file->error);
	}
	free(file->filename);
	free(file);
	return result;
}

