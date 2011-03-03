/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007 Bernhard R. Link
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
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>

#include "donefile.h"
#include "names.h"
#include "checksums.h"

retvalue donefile_isold(const char *filename, const struct checksums *expected) {
	char buffer[200];
	const char *start;
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
	start = buffer;
	/* for future extensibility: */
	while( *start == ':' ) {
		start++;
		while( *start != ' ' && *start != '\0' )
			start++;
		if( *start == ' ' )
			start++;
	}
	if( checksums_matches(expected, cs_md5sum, start) )
		return RET_NOTHING;
	else
		return RET_OK;
}

retvalue donefile_create(const char *filename, const struct checksums *checksums) {
	const char *md5sum;
	const char *start;
	size_t len;
	ssize_t written;
	int fd;
	char *donefilename;

	if( interrupted() )
		return RET_ERROR_INTERRUPTED;
	donefilename = calc_addsuffix(filename,"done");
	if( donefilename == NULL )
		return RET_ERROR_OOM;
	md5sum = checksums_getmd5sum(checksums);

	fd = open(donefilename, O_WRONLY|O_CREAT|O_TRUNC|O_NOCTTY|O_NOFOLLOW,
			0666);
	if( fd < 0 ) {
		int e = errno;
		fprintf(stderr, "Error creating file %s: %d=%s\n",
				donefilename, e, strerror(e));
		free(donefilename);
		return RET_ERRNO(e);
	}
	start = md5sum;
	len = strlen(md5sum);
	written = 0;
	while( len > 0 && (written=write(fd, start, len)) >= 0 ) {
		start += written;
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
