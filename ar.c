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
#include <assert.h>

#include <archive.h>

#include "error.h"
#include "uncompression.h"
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
	off_t member_size, next_position;
	void *readbuffer;
	/*@null@*/struct compressedfile *member;
	enum compression compression;
};

static ssize_t readwait(int fd, /*@out@*/void *buf, size_t count) {
	ssize_t totalread;

	totalread = 0;

	while (count > 0) {
		ssize_t s;

		s = read(fd, buf, count);
		if (s < 0)
			return s;
		if (interrupted()) {
			errno = EINTR;
			return -1;
		}
		if ((size_t)s > count) {
			errno = EINVAL;
			return -1;
		}
		if (s == 0)
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

	if (interrupted())
		return RET_ERROR_INTERRUPTED;
	ar = zNEW(struct ar_archive);
	if (FAILEDTOALLOC(ar))
		return RET_ERROR_OOM;
	ar->fd = open(filename, O_NOCTTY|O_RDONLY);
	if (ar->fd < 0) {
		int e = errno;
		fprintf(stderr, "Error %d opening %s: %s\n",
				e, filename, strerror(e));
		free(ar);
		return RET_ERRNO(e);
	}

	bytesread = readwait(ar->fd, buffer, sizeof(AR_MAGIC) - 1);
	if (bytesread != sizeof(AR_MAGIC)-1) {
		int e = errno;
		(void)close(ar->fd);
		free(ar);
		if (bytesread < 0) {
			fprintf(stderr, "Error %d reading from %s: %s\n",
					e, filename, strerror(e));
			return RET_ERRNO(e);
		} else {
			fprintf(stderr, "Premature end of reading from %s\n",
					filename);
			return RET_ERROR;
		}
	}
	if (memcmp(buffer, AR_MAGIC, sizeof(AR_MAGIC)-1) != 0) {
		(void)close(ar->fd);
		free(ar);
		fprintf(stderr,
"Missing ar header '!<arch>' at the beginning of %s\n",
				filename);
		return RET_ERROR;
	}
	ar->filename = strdup(filename);
	if (FAILEDTOALLOC(ar->filename)) {
		close(ar->fd);
		free(ar);
		return RET_ERROR_OOM;
	}
	ar->next_position = sizeof(AR_MAGIC) - 1;

	*n = ar;
	return RET_OK;
}

void ar_close(/*@only@*/struct ar_archive *ar) {
	if (ar != NULL) {
		if (ar->fd >= 0)
			(void)close(ar->fd);
		free(ar->filename);
		free(ar);
	}
}

/* RET_OK = next is there, RET_NOTHING = eof, < 0 = error */
retvalue ar_nextmember(struct ar_archive *ar, /*@out@*/char **filename) {
	ssize_t bytesread;
	char *p;
	off_t s;

	assert(ar->readbuffer == NULL);
	assert(ar->fd >= 0);

	/* seek over what is left from the last part: */
	s = lseek(ar->fd, ar->next_position, SEEK_SET);
	if (s == (off_t)-1) {
		int e = errno;
		fprintf(stderr,
"Error %d seeking to next member in ar file %s: %s\n",
				e, ar->filename, strerror(e));
		return RET_ERRNO(e);
	}
	/* read the next header from the file */

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	bytesread = readwait(ar->fd, &ar->currentheader,
			sizeof(ar->currentheader));
	ar->next_position += sizeof(ar->currentheader);
	if (bytesread == 0)
		return RET_NOTHING;
	if (bytesread != sizeof(ar->currentheader)){
		int e = errno;
		if (bytesread < 0) {
			fprintf(stderr,
"Error %d reading from ar file %s: %s\n",
				e, ar->filename, strerror(e));
			return RET_ERRNO(e);
		} else {
			fprintf(stderr, "Premature end of ar file %s\n",
					ar->filename);
			return RET_ERROR;
		}
	}
	if (memcmp(ar->currentheader.ah_magictrailer, AR_HEADERMAGIC, 2) != 0) {
		fprintf(stderr, "Corrupt ar file %s\n", ar->filename);
		return RET_ERROR;
	}

	/* calculate the length and mark possible fillers being needed */

	/* make ah_size null-terminated by overwriting the following field */
	assert (&ar->currentheader.ah_magictrailer[0]
			== ar->currentheader.ah_size + 10);
	ar->currentheader.ah_magictrailer[0] = '\0';

	ar->member_size = strtoul(ar->currentheader.ah_size, &p, 10);
	if (*p != '\0' && *p != ' ') {
		fprintf(stderr,
"Error calculating length field in ar file %s\n",
				ar->filename);
		return RET_ERROR;
	}
	ar->next_position += ar->member_size;
	if ((ar->member_size & 1) != 0)
		ar->next_position ++;

	/* get the name of the file */
	if (false) {
		/* handle long filenames */
		// TODO!
	} else {
		/* normal filenames */
		int i = sizeof(ar->currentheader.ah_filename);
		while (i > 0 && ar->currentheader.ah_filename[i-1] == ' ')
			i--;
		/* hop over GNU style filenames, though they should not
		 * be in a .deb file... */
		if (i > 0 && ar->currentheader.ah_filename[i-1] == '/')
			i--;
		*filename = strndup(ar->currentheader.ah_filename, i);
	}
	ar->compression = c_none;
	return RET_OK;
}

void ar_archivemember_setcompression(struct ar_archive *ar, enum compression compression) {
	ar->compression = compression;
}

ssize_t ar_archivemember_read(struct archive *a, void *d, const void **p) {
	struct ar_archive *ar = d;
	ssize_t bytesread;

	assert (ar->readbuffer != NULL);
	if (ar->member == NULL)
		return 0;

	*p = ar->readbuffer;
	bytesread = uncompress_read(ar->member, ar->readbuffer, BLOCKSIZE);
	if (bytesread < 0) {
		const char *msg;
		int e;

		// TODO: why _fdclose instead of _abort?
		(void)uncompress_fdclose(ar->member, &e, &msg);
		ar->member = NULL;
		archive_set_error(a, e, "%s", msg);
		return -1;
	}
	return bytesread;
}

int ar_archivemember_open(struct archive *a, void *d) {
	struct ar_archive *ar = d;
	retvalue r;
	const char *msg;
	int e;

	assert (uncompression_supported(ar->compression));

	assert (ar->readbuffer == NULL);
	ar->readbuffer = malloc(BLOCKSIZE);
	if (FAILEDTOALLOC(ar->readbuffer)) {
		archive_set_error(a, ENOMEM, "Out of memory");
		return ARCHIVE_FATAL;
	}
	r = uncompress_fdopen(&ar->member, ar->fd, ar->member_size,
			ar->compression, &e, &msg);
	if (RET_IS_OK(r))
		return ARCHIVE_OK;
	archive_set_error(a, e, "%s", msg);
	return ARCHIVE_FATAL;
}

int ar_archivemember_close(struct archive *a, void *d) {
	struct ar_archive *ar = d;
	retvalue r;
	const char *msg;
	int e;

	free(ar->readbuffer);
	ar->readbuffer = NULL;

	if (ar->member == NULL)
		return ARCHIVE_OK;

	r = uncompress_fdclose(ar->member, &e, &msg);
	ar->member = NULL;
	if (RET_IS_OK(r))
		return ARCHIVE_OK;
	archive_set_error(a, e, "%s", msg);
	return ARCHIVE_FATAL;
}
