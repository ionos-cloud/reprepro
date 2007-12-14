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
#include <stdint.h>
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
#include "sha1.h"
#include "md5sum.h"
#include "checksums.h"
#include "names.h"

extern int verbose;

static const char tab[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

retvalue md5sum_genstring(char **md5,struct MD5Context *context,off_t filesize) {
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

retvalue sha1_genstring(char **sha1, struct SHA1_Context *context) {
	unsigned char buffer[SHA1_DIGEST_SIZE];
	char result[2*SHA1_DIGEST_SIZE+1], *sha1sum;
	unsigned int i;

	SHA1Final(context, buffer);
	for( i = 0 ; i < SHA1_DIGEST_SIZE ; i++ ) {
		result[i<<1] = tab[buffer[i] >> 4];
		result[(i<<1)+1] = tab[buffer[i] & 0xF];
	}
	result[2*SHA1_DIGEST_SIZE] = '\0';
	sha1sum = mprintf("%s %lld", result, (long long)context->count);
	if( sha1sum == NULL )
		return RET_ERROR_OOM;
	*sha1 = sha1sum;
	return RET_OK;
}

retvalue checksum_dismantle(const char *combined, char *hashes[cs_count]) {
	const char *sha1start = NULL; size_t sha1len IFSTUPIDCC(= 0);
	const char *md5start; size_t md5len;
	const char *sizestart; size_t sizelen;
	const char *p;
	char *md5sum, *sha1sum;

	p = combined;
	while( *p == ':' ) {
		p++;
		if( p[0] == '1' && p[1] == ':' ) {
			p += 2;
			sha1start = p;
			while( *p != ' ' && *p != '\0' )
				p++;
			sha1len = p - sha1start;
		} else {
			while( *p != ' ' && *p != '\0' )
				p++;
		}
		if( *p == ' ' )
			p++;
	}
	md5start = p;
	while( *p != ' ' && *p != '\0' )
		p++;
	md5len = p - md5start;
	if( *p == ' ' )
		p++;
	sizestart = p;
	while( *p != ' ' && *p != '\0' )
		p++;
	sizelen = p - sizestart;
	if( sizelen == 0 || md5len == 0 ) {
		fprintf(stderr, "Internal Error: Invalid combined checksum '%s'!\n",
				combined);
		return RET_ERROR;
	}
	if( sha1start != NULL ) {
		sha1sum = malloc(sha1len+sizelen+2);
		if( sha1sum == NULL )
			return RET_ERROR_OOM;
		memcpy(sha1sum, sha1start, sha1len);
		sha1sum[sha1len] = ' ';
		memcpy(sha1sum + sha1len + 1, sizestart, sizelen);
		sha1sum[sha1len + 1 + sizelen] = '\0';
	} else
		sha1sum = NULL;
	md5sum = malloc(md5len+sizelen+2);
	if( md5sum == NULL ) {
		free(sha1sum);
		return RET_ERROR_OOM;
	}
	memcpy(md5sum, md5start, md5len);
	md5sum[md5len] = ' ';
	memcpy(md5sum + md5len + 1, sizestart, sizelen);
	md5sum[md5len + 1 + sizelen] = '\0';

	hashes[cs_md5sum] = md5sum;
	hashes[cs_sha1sum] = sha1sum;
	return RET_OK;
}

retvalue checksum_complete(const char *directory, const char *filename, char *hashes[cs_count]) {
	retvalue r;
	char *fullfilename, *realmd5sum, *realsha1sum;

	if( hashes[cs_md5sum] != NULL && hashes[cs_sha1sum] != NULL )
		return RET_OK;
	assert( hashes[cs_md5sum] != NULL || hashes[cs_sha1sum] != NULL );

	fullfilename = calc_dirconcat(directory, filename);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	r = checksum_read(fullfilename, &realmd5sum, &realsha1sum);
	free(fullfilename);
	if( !RET_IS_OK(r) )
		return r;
	if( hashes[cs_md5sum] != NULL &&
	    strcmp(hashes[cs_md5sum], realmd5sum) != 0 ) {
		fprintf(stderr,
"WARNING: '%s/%s' is different from recorded md5sum.\n"
"(This was only catched because some new checksum type was not yet available.)\n"
"Triggering recreation of that file.\n",
				directory,filename);
		free(realmd5sum); free(realsha1sum);
		return RET_NOTHING;
	}
	if( hashes[cs_sha1sum] != NULL &&
	    strcmp(hashes[cs_sha1sum], realsha1sum) != 0 ) {
		fprintf(stderr,
"WARNING: '%s/%s' is different from recorded sha1sum.\n"
"(This was only catched because some new checksum type was not yet available.)\n"
"Triggering recreation of that file.\n",
				directory,filename);
		free(realmd5sum); free(realsha1sum);
		return RET_NOTHING;
	}
	if( hashes[cs_md5sum] == NULL )
		hashes[cs_md5sum] = realmd5sum;
	else
		free(realmd5sum);
	if( hashes[cs_sha1sum] == NULL )
		hashes[cs_sha1sum] = realsha1sum;
	else
		free(realsha1sum);
	return RET_OK;
}

retvalue checksum_combine(char **combined, const char *hashes[cs_count]) {
	char *result;
	const char *separator;
	size_t sha1len, md5sizelen;

	assert( hashes[cs_md5sum] != NULL );
	if( hashes[cs_sha1sum] == NULL ) {
		result = strdup(hashes[cs_md5sum]);
		if( result == NULL )
			return RET_ERROR_OOM;
		*combined = result;
		return RET_OK;
	}
	separator = strchr(hashes[cs_sha1sum], ' ');
	if( separator == NULL ) {
		fprintf(stderr, "Internal Error: Malformed sha1sum '%s'!\n",
				hashes[cs_sha1sum]);
		return RET_ERROR;
	}
	sha1len = separator - hashes[cs_sha1sum];
	md5sizelen = strlen(hashes[cs_md5sum]);
	result = malloc(5+sha1len+md5sizelen);
	if( result == NULL )
		return RET_ERROR_OOM;
	memcpy(result, ":1:", 3);
	memcpy(result+3, hashes[cs_sha1sum], sha1len);
	result[3+sha1len] = ' ';
	memcpy(result+4+sha1len, hashes[cs_md5sum], md5sizelen+1);
	*combined = result;
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

retvalue checksum_read(const char *filename, char **md5sum, char **sha1sum){
	unsigned char buffer[16384];
	struct MD5Context md5_context;
	struct SHA1_Context sha1_context;
	int fd, e;
	off_t filesize;
	ssize_t sizeread;
	retvalue r;
	char *m;

	fd = open(filename,O_RDONLY);
	if( fd < 0 ) {
		e = errno;
		if( e == EACCES || e == ENOENT )
			return RET_NOTHING;
		else {
			fprintf(stderr,"Error opening '%s': %d=%m!\n", filename, e);
			return RET_ERRNO(e);
		}
	}

	filesize = 0;
	MD5Init(&md5_context);
	SHA1Init(&sha1_context);
	do {
		sizeread = read(fd, buffer, 16384);
		if( sizeread < 0 ) {
			e = errno;
			fprintf(stderr, "Error while reading: %m\n");
			close(fd);
			return RET_ERRNO(e);;
		}
		filesize += sizeread;
		if( sizeread > 0 ) {
			MD5Update(&md5_context, buffer, sizeread);
			SHA1Update(&sha1_context, buffer, sizeread);
		}
	} while( sizeread > 0 );
	(void)close(fd);
	r = md5sum_genstring(&m, &md5_context, filesize);
	assert( r != RET_NOTHING);
	if( RET_WAS_ERROR(r) )
		return r;
	r = sha1_genstring(sha1sum, &sha1_context);
	assert( r != RET_NOTHING);
	if( RET_WAS_ERROR(r) ) {
		free(m);
		return r;
	}
	*md5sum = m;
	return RET_OK;
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

retvalue md5sum_ensure(const char *fullfilename, const char *md5sum, bool warnifwrong) {
	retvalue ret;
	int fd,i;
	struct stat s;
	off_t expectedsize;
	retvalue r;

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
	r = calc_extractsize(md5sum, &expectedsize);

	/* if length cannot be parsed, just proceed, otherwise only
	 * calculare the actual file checksum, if the length matched */
	if( !RET_IS_OK(r) || s.st_size == expectedsize ) {
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
		MD5Update(&context,(const unsigned char*)data,len);
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
