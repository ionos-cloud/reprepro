#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "md5.h"

int md5sumAndSize(char *result,off_t *size,const char *filename,ssize_t bufsize){

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
		return fd;

	if( size ) {
		if( fstat(fd,&stat) != 0) {
			close(fd);
			return 3;
		}
		*size = stat.st_size;
	}
	
	if( bufsize <= 0 )
		bufsize = 16384;

	buffer = malloc(bufsize);

	if( ! buffer ) {
		close(fd);
		return 1;
	}
	
	MD5Init(&context);
	do {
		sizeread = read(fd,buffer,bufsize);
		if( sizeread < 0 ) {
			free(buffer);
			close(fd);
			return 2;
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
	return 0;
}

int md5sum_and_size(char **result,const char *filename,ssize_t bufsize){
	char md5Sum[33];
	off_t size;
	int ret;

	ret = md5sumAndSize(md5Sum,&size,filename,bufsize);
	if( ret != 0 ) {
		*result = NULL;
		return ret;
	} else {
		asprintf(result,"%s %ld",md5Sum,size);
		return 0;
	}
}

int md5sum(char *result,const char *filename,ssize_t bufsize){

	return md5sumAndSize(result,NULL,filename,bufsize);
}
