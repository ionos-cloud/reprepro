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

static inline retvalue builtin_gunzip(const char *compressed, const char *destination) {
	gzFile f;
	char buffer[4096];
	int bytes_read, bytes_written, written;
	int destfd;
	int e, i, zerror;

	f = gzopen(compressed, "r");
	if( f == NULL ) {
		// TODO: better error message...
		fprintf(stderr, "Could not read %s\n",
				compressed);
		return RET_ERROR;
	}
	destfd = open(destination, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
	if( destfd < 0 ) {
		e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, destination, strerror(e));
		gzclose(f);
		return RET_ERRNO(e);
	}
	do {
		bytes_read = gzread(f, buffer, 4096);
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
				gzclose(f);
				return RET_ERRNO(e);
			}
			bytes_written += written;
		}
	} while( true );
	e = errno;
	if( bytes_read < 0 ) {
		const char *msg = gzerror(f, &zerror);

		if( zerror != Z_ERRNO ) {
			fprintf(stderr, "Zlib Error %d reading from '%s': %s!\n",
					zerror, compressed, msg);
			(void)close(destfd);
			return RET_ERROR;
		}
		e = errno;
		(void)gzclose(f);
	} else {
		zerror = gzclose(f);
		e = errno;
	}
	if( zerror == Z_ERRNO ) {
		fprintf(stderr, "Error %d reading from '%s': %s!\n",
				e, compressed, strerror(e));
		(void)close(destfd);
		return RET_ERRNO(e);
	} else if( zerror < 0 ) {
		fprintf(stderr, "Zlib Error %d reading from '%s'!\n",
				zerror, compressed);
		(void)close(destfd);
		return RET_ERROR;
	}
	i = close(destfd);
	if( i != 0 ) {
		e = errno;
		fprintf(stderr, "Error %d writing to '%s': %s!\n",
				e, destination, strerror(e));
		return RET_ERROR;
	}
	return RET_OK;
}

#ifdef HAVE_LIBBZ2
static inline retvalue builtin_bunzip2(const char *compressed, const char *destination) {
	BZFILE *f;
	char buffer[4096];
	int bytes_read, bytes_written, written;
	int destfd;
	int e, i, zerror;

	f = BZ2_bzopen(compressed, "r");
	if( f == NULL ) {
		// TODO: better error message...
		fprintf(stderr, "Could not read %s\n",
				compressed);
		return RET_ERROR;
	}
	destfd = open(destination, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
	if( destfd < 0 ) {
		e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, destination, strerror(e));
		BZ2_bzclose(f);
		return RET_ERRNO(e);
	}
	do {
		bytes_read = BZ2_bzread(f, buffer, 4096);
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
				BZ2_bzclose(f);
				return RET_ERRNO(e);
			}
			bytes_written += written;
		}
	} while( true );
	e = errno;
	if( bytes_read < 0 ) {
		const char *msg = BZ2_bzerror(f, &zerror);

		if( zerror != Z_ERRNO ) {
			fprintf(stderr, "libbz2 Error %d reading from '%s': %s!\n",
					zerror, compressed, msg);
			(void)close(destfd);
			return RET_ERROR;
		}
		e = errno;
		(void)BZ2_bzclose(f);
	} else {
		BZ2_bzclose(f);
		e = errno;
		zerror = 0;
	}
	if( zerror == Z_ERRNO ) {
		fprintf(stderr, "Error %d reading from '%s': %s!\n",
				e, compressed, strerror(e));
		(void)close(destfd);
		return RET_ERRNO(e);
	} else if( zerror < 0 ) {
		fprintf(stderr, "libbz2 Error %d reading from '%s'!\n",
				zerror, compressed);
		(void)close(destfd);
		return RET_ERROR;
	}
	i = close(destfd);
	if( i != 0 ) {
		e = errno;
		fprintf(stderr, "Error %d writing to '%s': %s!\n",
				e, destination, strerror(e));
		return RET_ERROR;
	}
	return RET_OK;
}
#endif

retvalue uncompress_queue_file(const char *compressed, const char *destination, enum compression compression, finishaction *action, void *privdata, bool *done_p) {
	retvalue r;

	if( verbose > 1 ) {
		fprintf(stderr, "Uncompress '%s' into '%s'...\n",
				compressed, destination);
	}


	(void)unlink(destination);
	switch( compression ) {
		case c_gzip:
			r = builtin_gunzip(compressed, destination);
			break;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			r = builtin_bunzip2(compressed, destination);
			break;
#endif
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
			r = builtin_gunzip(compressed, destination);
			break;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			r = builtin_bunzip2(compressed, destination);
			break;
#endif
		default:
			assert( compression != compression );
	}
	if( RET_WAS_ERROR(r) ) {
		(void)unlink(destination);
		return r;
	}
	return RET_OK;
}
