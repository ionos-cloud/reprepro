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

#include <errno.h>
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

static retvalue md5sum_genstring(char **md5,struct MD5Context *context,off_t filesize) {
static char tab[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
	unsigned char buffer[16];
	char result[33],*md5sum;
	unsigned int i;

	MD5Final(buffer,context);
	for(i=0;i<16;i++) {
		result[i<<1] = tab[buffer[i] >> 4];
		result[(i<<1)+1] = tab[buffer[i] & 0xF];
	}
	result[32] = '\0';

	md5sum = mprintf("%s %ld",result,filesize);
	if( ! md5sum )
		return RET_ERROR_OOM;
	*md5 = md5sum;
	return RET_OK;
}

static retvalue md5sum_calc(int infd,int outfd, char **result, size_t bufsize) {
	struct MD5Context context;
	unsigned char *buffer;
	off_t filesize;
	ssize_t sizeread;
	int ret;

	if( bufsize <= 0 )
		bufsize = 16384;

	buffer = malloc(bufsize);
	if( buffer == NULL ) {
		return RET_ERROR_OOM;
	}

	filesize = 0;
	MD5Init(&context);
	do {
		sizeread = read(infd,buffer,bufsize);
		if( sizeread < 0 ) {
			ret = errno;
			fprintf(stderr,"Error while reading: %m\n");
			free(buffer);
			return RET_ERRNO(ret);;
		}
		filesize += sizeread;
		if( sizeread > 0 ) {
			ssize_t written;

			MD5Update(&context,buffer,sizeread);
			if( outfd >= 0 ) {
				written = write(outfd,buffer,sizeread);
				if( written < 0 ) {
					ret = errno;
					fprintf(stderr,"Error while writing: %m\n");
					free(buffer);
   					return RET_ERRNO(ret);;
				}
				if( written != sizeread ) {
					fprintf(stderr,"Error while writing!\n");
					free(buffer);
   					return RET_ERROR;
				}
			}
		}
	} while( sizeread > 0 );
	free(buffer);
	return md5sum_genstring(result,&context,filesize);

}

retvalue md5sum_and_size(char **result,const char *filename,ssize_t bufsize){
	retvalue ret;
	int fd;

	assert(result != NULL);

	fd = open(filename,O_RDONLY);
	if( fd < 0 ) 
		return RET_ERRNO(-fd);

	ret = md5sum_calc(fd,-1,result,bufsize);
	close(fd);
	if( RET_IS_OK(ret) ) {
		return RET_OK;
	} else {
		*result = NULL;
		return ret;
	}
}
