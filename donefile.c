/*  This file is part of "reprepro"
 *  Copyright (C) 2006 Bernhard R. Link
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

#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>

#include "donefile.h"
#include "names.h"

retvalue donefile_isold(const char *filename, const char *expected) {
	char buffer[200];
	size_t len;
	ssize_t bytes;
	int fd;
	char *donefilename = calc_addsuffix(filename,"done");
	if( donefilename == NULL )
		return RET_ERROR_OOM;
	fd = open(donefilename, O_RDONLY|O_NOCTTY|O_NOFOLLOW, 0666);
	if( fd < 0 ) {
		int e = errno;
		if( e == EACCES || e == ENOENT ) {
			free(donefilename);
			return RET_OK;
		}
		fprintf(stderr, "Error opening file %s: %d=%s\n",
				donefilename, e, strerror(e));
		free(donefilename);
		return RET_ERRNO(e);
	}
	len = 0;
	while( len < 199 && (bytes=read(fd, buffer+len, 199-len)) > 0 ) {
		len += bytes;
	}
	if( bytes > 0 ) {
		fprintf(stderr, "Unexpected long file %s!\n", donefilename);
		(void)close(fd);
		free(donefilename);
		return RET_ERROR;
	} else if( bytes < 0 ) {
		int e = errno;
		fprintf(stderr, "Error reading file %s: %d=%s\n",
				donefilename, e, strerror(e));
		(void)close(fd);
		free(donefilename);
		return RET_ERRNO(e);
	}
	free(donefilename);
	(void)close(fd);
	buffer[len] = '\0';
	if( strcmp(expected, buffer) == 0 )
		return RET_NOTHING;
	else
		return RET_OK;
}

retvalue donefile_create(const char *filename, const char *expected) {
	size_t len;
	ssize_t written;
	int fd;
	if( interrupted() )
		return RET_ERROR_INTERUPTED;
	char *donefilename = calc_addsuffix(filename,"done");

	if( donefilename == NULL )
		return RET_ERROR_OOM;
	fd = open(donefilename, O_WRONLY|O_CREAT|O_TRUNC|O_NOCTTY|O_NOFOLLOW,
			0666);
	if( fd < 0 ) {
		int e = errno;
		fprintf(stderr, "Error creating file %s: %d=%s\n",
				donefilename, e, strerror(e));
		free(donefilename);
		return RET_ERRNO(e);
	}
	len = strlen(expected);
	written = 0;
	while( len > 0 && (written=write(fd, expected, len)) >= 0 ) {
		expected += written;
		assert( len >= (size_t)written );
		len -= written;
	}
	if( written < 0 ) {
		int e = errno;
		fprintf(stderr, "Error writing into %s: %d=%s\n",
				donefilename, e, strerror(e));
		free(donefilename);
		(void)close(fd);
		return RET_ERRNO(e);
	}
	if( close(fd) != 0 ) {
		int e = errno;
		fprintf(stderr, "Error writing %s: %d=%s\n",
				donefilename, e, strerror(e));
		free(donefilename);
		return RET_ERRNO(e);
	}
	free(donefilename);
	return RET_OK;
}

void donefile_delete(const char *filename) {
	char *donefilename = calc_addsuffix(filename,"done");
	unlink(donefilename);
	free(donefilename);
}
