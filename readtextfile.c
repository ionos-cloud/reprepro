/*  This file is part of "reprepro"
 *  Copyright (C) 2007 Bernhard R. Link
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
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "error.h"
#include "names.h"
#include "chunks.h"
#include "readtextfile.h"

/* This file supplies code to read a text file (.changes, .dsc, Release, ...)
 * into a chunk, warning if it is too long or if it contains binary data */

static bool isbinarydata(const char *buffer, size_t len, const char *source) {
	size_t i;
	unsigned char c;

	for (i = 0 ; i < len ; i++) {
		c = (unsigned char)buffer[i];
		if (c < ' ' && c != '\t' && c != '\n' && c != '\r') {
			fprintf(stderr,
"Unexpected binary character \\%03hho in %s\n",
					c, source);
			return true;
		}
	}
	return false;
}

retvalue readtextfilefd(int fd, const char *source, char **data, size_t *len) {
	size_t buffersize = 102400, readdata = 0;
	ssize_t readbytes;
	char *buffer, *h;

	buffer = malloc(buffersize);
	if (FAILEDTOALLOC(buffer))
		return RET_ERROR_OOM;
	errno = 0;
	while ((readbytes = read(fd, buffer + readdata, buffersize-readdata))
			> 0) {

		/* text files are normally small, so it does not hurt to check
		 * the whole of them always */
		if (isbinarydata(buffer + readdata, (size_t)readbytes, source)) {
			free(buffer);
			return RET_ERROR;
		}
		readdata += readbytes;
		assert (readdata <= buffersize);
		if (readdata + 1024 >= buffersize) {
			if (buffersize >= 10*1024*1024) {
				fprintf(stderr, "Ridiculously large %s\n", source);
				free(buffer);
				return RET_ERROR;
			}
			buffersize += 51200;
			h = realloc(buffer, buffersize);
			if (FAILEDTOALLOC(h)) {
				free(buffer);
				return RET_ERROR_OOM;
			}
			buffer = h;
		}
	}
	if (readbytes < 0) {
		int e = errno;
		free(buffer);
		fprintf(stderr, "Error reading %s: %s\n", source,
				strerror(e));
		return RET_ERRNO(e);
	}
	h = realloc(buffer, readdata + 1);
	if (h == NULL) {
#ifdef SPLINT
		h = NULL;
#endif
		if (readdata >= buffersize) {
			free(buffer);
			return RET_ERROR_OOM;
		}
	} else
		buffer = h;
	buffer[readdata] = '\0';
	*data = buffer;
	if (len != NULL)
		*len = readdata;
	return RET_OK;
}

retvalue readtextfile(const char *source, const char *sourcetoshow, char **data, size_t *len) {
	int fd; char *buffer; size_t bufferlen;
	retvalue r;
	int ret;

	fd = open(source, O_RDONLY|O_NOCTTY);
	if (fd < 0) {
		int e = errno;
		fprintf(stderr, "Error opening '%s': %s\n",
				sourcetoshow, strerror(e));
		return RET_ERRNO(e);
	}
	r = readtextfilefd(fd, sourcetoshow, &buffer, &bufferlen);
	if (!RET_IS_OK(r)) {
		(void)close(fd);
		return r;
	}
	ret = close(fd);
	if (ret != 0) {
		int e = errno;
		free(buffer);
		fprintf(stderr, "Error reading %s: %s\n", sourcetoshow,
				strerror(e));
		return RET_ERRNO(e);
	}
	*data = buffer;
	if (len != NULL)
		*len = bufferlen;
	return RET_OK;
}
