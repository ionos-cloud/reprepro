/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2005 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "dirs.h"
#include "md5.h"
#include "md5sum.h"
#include "names.h"

extern int verbose;

retvalue md5sum_genstring(char **md5,struct MD5Context *context,off_t filesize) {
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

	md5sum = mprintf("%s %lld",result,(long long)filesize);
	if( md5sum == NULL )
		return RET_ERROR_OOM;
	*md5 = md5sum;
	return RET_OK;
}

static retvalue md5sum_calc(int infd,int outfd, /*@out@*/char **result, size_t bufsize) {
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

retvalue md5sum_read(const char *filename,char **result){
	retvalue ret;
	int fd,i;

	assert(result != NULL);

	fd = open(filename,O_RDONLY);
	if( fd < 0 ) {
		i = errno;
		if( i == EACCES || i == ENOENT )
			return RET_NOTHING;
		else {
			fprintf(stderr,"Error opening '%s': %d=%m!\n",filename,i);
			return RET_ERRNO(i);
		}
	}

	ret = md5sum_calc(fd,-1,result,0);
	close(fd);
	if( RET_IS_OK(ret) ) {
		return RET_OK;
	} else {
		*result = NULL;
		return ret;
	}
}

retvalue md5sum_copy(const char *origfilename,const char *destfilename,
			char **result){
	retvalue r;
	int fdr,fdw;
	int i;

	assert(result != NULL);

	r = dirs_make_parent(destfilename);
	if( RET_WAS_ERROR(r) )
		return r;

	fdr = open(origfilename,O_RDONLY);
	if( fdr < 0 ) {
		i = errno;
		if( i  == EACCES || i == ENOENT )
			return RET_NOTHING;
		fprintf(stderr,"Error opening '%s': %d=%m\n",origfilename,i);
		return RET_ERRNO(i);
	}
	fdw = open(destfilename,O_NOCTTY|O_WRONLY|O_CREAT|O_EXCL,0666);
	if( fdw < 0 ) {
		i = errno;
		if( i == EEXIST ) {
			close(fdr);
			return RET_ERROR_EXIST;
		}
		fprintf(stderr,"Error creating '%s': %d=%m\n",destfilename,i);
		close(fdr);
		return RET_ERRNO(i);
	}


	r = md5sum_calc(fdr,fdw,result,0);
	close(fdr);
	i = close(fdw);
	if( RET_WAS_ERROR(r) ) {
		*result = NULL;
		return r;
	}
	if( i < 0 ) {
		i = errno;
		fprintf(stderr,"Error writing to '%s': %d=%m\n",destfilename,i);
		unlink(destfilename);
		free(*result);
		*result = NULL;
		return RET_ERRNO(i);
	}
	return RET_OK;
}
/* same as above, but delete existing files and try to hardlink first. */
retvalue md5sum_place(const char *origfilename,const char *destfilename,
			char **result) {
	int i;
	retvalue r;

	r = dirs_make_parent(destfilename);
	if( RET_WAS_ERROR(r) )
		return r;

	unlink(destfilename);
	i = link(origfilename,destfilename);
	if( i == 0 ) {
		return md5sum_read(destfilename,result);
	} else {
		if( verbose > 1 ) {
			fprintf(stderr,"Linking failed, copying file '%s' to '%s'...\n",origfilename,destfilename);
		}

		r = md5sum_copy(origfilename,destfilename,result);
		if( r == RET_ERROR_EXIST ) {
			fprintf(stderr,"File '%s' already exists and could not be removed to link/copy '%s' there!\n",destfilename,origfilename);

		}
		return r;
	}
}

retvalue md5sum_ensure(const char *fullfilename,const char *md5sum,bool_t warnifwrong) {
	retvalue ret;
	int fd,i;
	struct stat s;
	off_t expectedsize;

	assert(md5sum != NULL);

	fd = open(fullfilename,O_RDONLY);
	if( fd < 0 ) {
		i = errno;
		if( i == EACCES || i == ENOENT )
			return RET_NOTHING;
		else {
			fprintf(stderr,"Error opening '%s': %d=%m!\n",fullfilename,i);
			return RET_ERRNO(i);
		}
	}
	i = fstat(fd,&s);
	if( i < 0 ) {
		i = errno;
		fprintf(stderr,"Error stating '%s': %d=%m!\n",fullfilename,i);
		close(fd);
		return RET_ERRNO(i);
	}
	// TODO: extract filesize from md5sum and compare here instead
	// and change code below to check this number and not generate the
	// full string...
	expectedsize = s.st_size;

	if( s.st_size == expectedsize ) {
		char *foundmd5sum;

		ret = md5sum_calc(fd,-1,&foundmd5sum,0);
		close(fd);
		if( RET_IS_OK(ret) ) {
			if( strcmp(md5sum,foundmd5sum) == 0 ) {
				free(foundmd5sum);
				return RET_OK;
			}
			if( warnifwrong )
				fprintf(stderr,"Unknown file \"%s\" has other md5sum (%s) than expected(%s), deleting it!\n",fullfilename,foundmd5sum,md5sum);
			free(foundmd5sum);
		}
	} else {
		close(fd);
		if( warnifwrong )
			fprintf(stderr,"Unknown file \"%s\" has other size (%llx) than expected(%llx), deleting it!\n",fullfilename,(long long)s.st_size,(long long)expectedsize);
	}

	if( unlink(fullfilename) == 0 ) {
		return RET_NOTHING;
	}
	fprintf(stderr,"Could not delete '%s' out of the way!\n",fullfilename);
	return RET_ERROR_WRONG_MD5;
}


retvalue md5sum_replace(const char *filename, const char *data, size_t len, char **result){
	struct MD5Context context;
	size_t todo; const char *towrite;
	char *tempfilename;
	int fd, ret;
	retvalue r;
	char *md5sum;

	tempfilename = calc_addsuffix(filename,"new");
	if( tempfilename == NULL )
		return RET_ERROR_OOM;

	fd = open(tempfilename, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
	if( fd < 0 ) {
		int e = errno;
		fprintf(stderr, "ERROR creating '%s': %s\n", tempfilename,
				strerror(e));
		free(tempfilename);
		return RET_ERRNO(e);
	}

	todo = len; towrite = data;
	while( todo > 0 ) {
		ssize_t written = write(fd, towrite, todo);
		if( written >= 0 ) {
			todo -= written;
			towrite += written;
		} else {
			int e = errno;
			close(fd);
			fprintf(stderr, "Error writing to '%s': %s\n",
					tempfilename, strerror(e));
			unlink(tempfilename);
			free(tempfilename);
			return RET_ERRNO(e);
		}
	}
	ret = close(fd);
	if( ret < 0 ) {
		int e = errno;
		fprintf(stderr, "Error writing to '%s': %s\n",
				tempfilename, strerror(e));
		unlink(tempfilename);
		free(tempfilename);
		return RET_ERRNO(e);
	}

	if( result != NULL ) {
		MD5Init(&context);
		MD5Update(&context,data,len);
		r = md5sum_genstring(&md5sum, &context,len);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			unlink(tempfilename);
			free(tempfilename);
			return r;
		}
	} else
		md5sum = NULL;
	ret = rename(tempfilename, filename);
	if( ret < 0 ) {
		int e = errno;
		free(md5sum);
		fprintf(stderr, "Error moving '%s' to '%s': %s\n",
				tempfilename, filename,  strerror(e));
		unlink(tempfilename);
		free(tempfilename);
		return RET_ERRNO(e);
	}
	free(tempfilename);
	if( result != NULL )
		*result = md5sum;
	return RET_OK;
}
