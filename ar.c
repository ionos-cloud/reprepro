/*  This file is part of "reprepro"
 *  Copyright (C) 2005,2006 Bernhard R. Link
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <assert.h>

#include <archive.h>

#include "error.h"
#include "ar.h"

/* Arr, me matees, Arr */

#define BLOCKSIZE 10240
#define AR_MAGIC "!<arch>\n"
#define AR_HEADERMAGIC "`\n"

struct ar_archive {
	char *filename;
	int fd;
	struct ar_header {
		char ah_filename[16];
		char ah_date[12];
		char ah_uid[6];
		char ah_gid[6];
		char ah_mode[8];
		char ah_size[10];
		char ah_magictrailer[2];
	} currentheader;
	off_t bytes_left;
	void *readbuffer;
	bool_t wasodd;
};

static ssize_t readwait(int fd, void *buf, size_t count) {
	ssize_t totalread;

	totalread = 0;

	while( count > 0 ) {
		ssize_t s;

		s = read(fd,buf,count);
		if( s < 0 )
			return s;
		if( (size_t)s > count ) {
			errno = EINVAL;
			return -1;
		}
		if( s == 0 )
			break;
		totalread += s;
		buf += s;
		count -= s;
	}
	return totalread;
}

retvalue ar_open(/*@out@*/struct ar_archive **n, const char *filename) {
	struct ar_archive *ar;
	char buffer[sizeof(AR_MAGIC)];
	ssize_t bytesread;

	ar = calloc(1,sizeof(struct ar_archive));
	if( ar == NULL )
		return RET_ERROR_OOM;
	ar->fd = open(filename,O_NOCTTY|O_RDONLY);
	if( ar->fd < 0 ) {
		int e = errno;
		fprintf(stderr,"Error opening %s: %m\n",filename);
		free(ar);
		return RET_ERRNO(e);
	}

	bytesread = readwait(ar->fd,buffer,sizeof(AR_MAGIC)-1);
	if( bytesread != sizeof(AR_MAGIC)-1 ) {
		int e = errno;
		(void)close(ar->fd);
		free(ar);
		if( bytesread < 0 ) {
			fprintf(stderr,"Error reading from %s: %s\n",filename,strerror(e));
			return RET_ERRNO(e);
		} else {
			fprintf(stderr,"Premature end of reading from %s\n",filename);
			return RET_ERROR;
		}
	}
	if( memcmp(buffer,AR_MAGIC,sizeof(AR_MAGIC)-1) != 0 ) {
		(void)close(ar->fd);
		free(ar);
		fprintf(stderr,"Missing ar-header '!<arch>' at the beginning of %s\n",filename);
		return RET_ERROR;
	}
	ar->filename = strdup(filename);
	if( ar->filename == NULL ) {
		close(ar->fd);
		free(ar);
		return RET_ERROR_OOM;
	}

	*n = ar;
	return RET_OK;
}

void ar_close(/*@only@*/struct ar_archive *ar) {
	if( ar != NULL ) {
		if( ar->fd >= 0 )
			(void)close(ar->fd);
		free(ar->filename);
		free(ar);
	}
}

/* RET_OK = next is there, RET_NOTHING = eof, < 0 = error */
retvalue ar_nextmember(struct ar_archive *ar,/*@out@*/char **filename) {
	ssize_t bytesread;
	char *p;

	assert(ar->readbuffer == NULL);
	assert(ar->fd >= 0);

	/* seek over what is left from the last part: */

	if( ar->bytes_left >0 || ar->wasodd ) {
		off_t s;
		s = lseek(ar->fd,ar->bytes_left+ar->wasodd,SEEK_CUR);
		if( s == (off_t)-1 ) {
			int e = errno;
			fprintf(stderr,"Error seeking to next member in ar-file %s: %s\n",ar->filename,strerror(e));
			return RET_ERRNO(e);
		}
	}
	/* read the next header from the file */

	bytesread = readwait(ar->fd,&ar->currentheader,sizeof(ar->currentheader));
	if( bytesread == 0 )
		return RET_NOTHING;
	if( bytesread != sizeof(ar->currentheader) ){
		int e = errno;
		if( bytesread < 0 ) {
			fprintf(stderr,"Error reading from ar-file %s: %s\n",ar->filename,strerror(e));
			return RET_ERRNO(e);
		} else {
			fprintf(stderr,"Premature end of ar-file %s\n",ar->filename);
			return RET_ERROR;
		}
	}
	if( memcmp(ar->currentheader.ah_magictrailer,AR_HEADERMAGIC,2) != 0 ) {
		fprintf(stderr,"Corrupt ar-file %s\n",ar->filename);
		return RET_ERROR;
	}
	
	/* calculate the length and mark possible fillers being needed */

	ar->currentheader.ah_size[11] = '\0'; // ugly, but it works

	ar->bytes_left = strtoul(ar->currentheader.ah_size,&p,10);
	if( *p != '\0' && *p != ' ' ) {
		fprintf(stderr,"Error calculating length field in ar-file %s\n",ar->filename);
		return RET_ERROR;
	}
	if( (ar->bytes_left & 1) != 0 ) 
		ar->wasodd = TRUE;

	/* get the name of the file */
	if( FALSE ) {
		/* handle long filenames */
		// TODO!
	} else {
		/* normal filenames */
		int i = sizeof(ar->currentheader.ah_filename);
		while( i > 0 && ar->currentheader.ah_filename[i-1] == ' ')
			i--;
		*filename = strndup(ar->currentheader.ah_filename,i);
	}
	return RET_OK;
}

ssize_t ar_archivemember_read(struct archive *a, void *d, const void **p) {
	struct ar_archive *ar = d;
	ssize_t bytesread;

	assert( ar->readbuffer != NULL );
	if( ar->bytes_left == 0 )
		return 0;

	*p = ar->readbuffer;
	bytesread = read(ar->fd,ar->readbuffer,(ar->bytes_left > BLOCKSIZE)?BLOCKSIZE:ar->bytes_left);
	if( bytesread < 0 ) {
		archive_set_error(a,errno,"Error reading from file: %m");
		return -1;
	}
	if( bytesread == 0 ) {
		archive_set_error(a,EIO,"Unexpected end of file");
		return -1;
	}
	ar->bytes_left -= bytesread;
	return bytesread;
}

int ar_archivemember_open(struct archive *a, void *d) {
	struct ar_archive *ar = d;

	assert(ar->readbuffer == NULL );
	ar->readbuffer = malloc(BLOCKSIZE);
	if( ar->readbuffer == NULL ) {
		archive_set_error(a,ENOMEM,"Out of memory");
		return ARCHIVE_FATAL;
	}
	return ARCHIVE_OK;
}

int ar_archivemember_close(UNUSED(struct archive *a), void *d) {
	struct ar_archive *ar = d;

	free(ar->readbuffer);
	ar->readbuffer = NULL;
	return ARCHIVE_OK;
}
