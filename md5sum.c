/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2003 Bernhard R. Link
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include "error.h"
#include "mprintf.h"
#include "md5sum.h"
#include "md5.h"

static retvalue md5sumAndSize(char *result,off_t *size,const char *filename,ssize_t bufsize){

	struct MD5Context context;
	unsigned char *buffer;
	ssize_t sizeread;
	int fd,i;
	struct stat stat;
static char tab[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

	/* it would be nice to distinguish between a non-existant file and errors,
	 * though a stat before a open might introduce timing-attacks. perhaps making
	 * a stat after an open failed might be the way to go. */
	fd = open(filename,O_RDONLY);
	if( fd < 0 ) 
		return RET_ERRNO(-fd);

	if( size ) {
		if( fstat(fd,&stat) != 0) {
			close(fd);
			return RET_ERROR;
		}
		*size = stat.st_size;
	}
	
	if( bufsize <= 0 )
		bufsize = 16384;

	buffer = malloc(bufsize);

	if( ! buffer ) {
		close(fd);
		return RET_ERROR_OOM;
	}
	
	MD5Init(&context);
	do {
		sizeread = read(fd,buffer,bufsize);
		if( sizeread < 0 ) {
			free(buffer);
			close(fd);
			return RET_ERROR;
		}
		if( sizeread > 0 )
			MD5Update(&context,buffer,sizeread);
	} while( sizeread > 0 );
	close(fd);
	MD5Final(buffer,&context);
	for(i=0;i<16;i++) {
		result[i<<1] = tab[buffer[i] >> 4];
		result[(i<<1)+1] = tab[buffer[i] & 0xF];
	}
	free(buffer);
	result[32] = '\0';
	return RET_OK;
}

retvalue md5sum_and_size(char **result,const char *filename,ssize_t bufsize){
	char md5Sum[33];
	off_t size;
	retvalue ret;

	assert(result != NULL);
	ret = md5sumAndSize(md5Sum,&size,filename,bufsize);
	if( RET_IS_OK(ret) ) {
		*result = mprintf("%s %ld",md5Sum,size);
		if( ! *result )
			return RET_ERROR_OOM;
		return RET_OK;
	} else {
		*result = NULL;
		return ret;
	}
}

retvalue md5sum(char *result,const char *filename,ssize_t bufsize){

	return md5sumAndSize(result,NULL,filename,bufsize);
}
