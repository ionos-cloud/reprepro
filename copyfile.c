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
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#include "error.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "md5sum.h"
#include "copyfile.h"

//TODO: think about, if calculating md5sum and copying could be done in
//the same step. (the data is pulled once through the memory anyway...

static retvalue copyfile(const char *destfullfilename,const char *origfile) {
	int ret,fd,fdw;
	retvalue r;
	struct stat stat;
	ssize_t written;
	void *content;

	fd = open(origfile,O_NOCTTY|O_RDONLY);
	if( fd < 0 ) {
		ret = errno;
		fprintf(stderr,"Error opening '%s': %m\n",origfile);
		return RET_ERRNO(ret);
	}

	ret = fstat(fd,&stat);
	if( ret < 0 ) {
		ret = errno;
		fprintf(stderr,"Error stat'ing '%s': %m\n",origfile);
		close(fd);
		return RET_ERRNO(ret);
	}
		
	content = mmap(NULL,stat.st_size,PROT_READ,MAP_SHARED,fd,0);
	if( content == MAP_FAILED ) {
		ret = errno;
		fprintf(stderr,"Error mmap'ing '%s': %m\n",origfile);
		close(fd);
		return RET_ERRNO(ret);
	}

	r = dirs_make_parent(destfullfilename);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error creating '%s': %m\n",destfullfilename);
		close(fd);
		return r;

	}

	fdw = open(destfullfilename,O_NOCTTY|O_WRONLY|O_CREAT|O_EXCL,0777);
	if( fdw < 0 ) {
		ret = errno;
		if( ret == EEXIST ) {
			close(fd);
			return RET_NOTHING;
		} else {
			fprintf(stderr,"Error creating '%s': %m\n",destfullfilename);
			close(fd);
			return RET_ERRNO(ret);
		}
	}

	written = write(fdw,content,stat.st_size);
	if( written < stat.st_size ) {
		ret = errno;
		fprintf(stderr,"Error while writing to '%s': %m\n",destfullfilename);
		close(fd);
		close(fdw);
		unlink(destfullfilename);
		return RET_ERRNO(ret);
	}

	ret = close(fdw);
	if( ret < 0 ) {
		ret = errno;
		fprintf(stderr,"Error writing to '%s': %m\n",destfullfilename);
		close(fd);
		unlink(destfullfilename);
		return RET_ERRNO(ret);
	}

	ret = munmap(content,stat.st_size);
	if( ret < 0 ) {
		ret = errno;
		fprintf(stderr,"Error munmap'ing '%s': %m\n",origfile);
		close(fd);
		return RET_ERRNO(ret);
	}
	close(fd);
	return RET_OK;
}

/* Make sure <mirrordir>/<filekey> has <md5expected>, and we can get
 * there by copying <origfile> there. */
retvalue copyfile_md5known(const char *mirrordir,const char *filekey,const char *origfile,const char *md5expected) {
	retvalue r;
	char *fullfilename;
	char *md5sum,*md5sum2;	

	fullfilename = calc_dirconcat(mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;

	r = copyfile(fullfilename,origfile);
	if( r == RET_NOTHING ) {
		/* The file already exists, check if it's md5sum fits */
		r = md5sum_and_size(&md5sum,fullfilename,0);
		if( RET_WAS_ERROR(r) ) {
			free(fullfilename);
			return r;
		}
		if( strcmp(md5sum,md5expected) == 0 ) {
			free(md5sum);
			free(fullfilename);
			return RET_OK;
		} else {
			int ret;

			/* There already is a file there, as
			 * we should have queried the db, this
			 * means this is a leftover file, we
			 * just delete and try again... */
			fprintf(stderr,"There already is a non-registered file '%s' with the wrong md5sum ('%s', but expect '%s'). Removing it...\n",fullfilename,md5sum,md5expected);
			free(md5sum);
			ret = unlink(fullfilename);
			if( ret < 0 ) {
				ret = errno;
				fprintf(stderr,"Error trying to delete '%s': %m\n",fullfilename);
				free(fullfilename);
				return RET_ERRNO(ret);
			}
			/* try again: */
			r = copyfile(fullfilename,origfile);
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Cannot create file '%s'. create says is still exists, though it was deleted.\n",fullfilename);
				r = RET_ERRNO(EEXIST);
			}
		}
	} 
	if( RET_WAS_ERROR(r) ) {
		free(fullfilename);
		return r;
	}
	r = md5sum_and_size(&md5sum,fullfilename,0);
	if( RET_WAS_ERROR(r) ) {
		unlink(fullfilename);
		free(fullfilename);
		return r;
	}
	if( strcmp(md5sum,md5expected) == 0 ) {
		free(md5sum);
		free(fullfilename);
		return RET_OK;
	} else {
		/* this is not what we expect, perhaps the origfile
		 * is already bogus */
		unlink(fullfilename);
		r = md5sum_and_size(&md5sum2,origfile,0);
		if( RET_WAS_ERROR(r) ) {
			free(md5sum);
			free(fullfilename);
			return r;
		}
		if( strcmp(md5sum,md5expected) == 0 ) {
			fprintf(stderr,"There seems to occoured an error while copying '%s' to '%s' (md5sums did not match).\n",origfile,fullfilename);
		} else {
			fprintf(stderr,"'%s' has md5sum '%s', while '%s' was expected.\n",origfile,md5sum2,md5expected);
		}
		free(md5sum2);free(md5sum);
		free(fullfilename);
		return RET_ERROR_WRONG_MD5;
	}
}

retvalue copyfile_getmd5(const char *mirrordir,const char *filekey,const char *origfile,char **md5sum) {
	retvalue r;
	char *fullfilename;

	fullfilename = calc_dirconcat(mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;

	r = copyfile(fullfilename,origfile);
	if( r == RET_NOTHING ) {
		/* there is already a file of that name */
		// TODO: compare their md5sums and perhaps
		// delete and recopy...
		fprintf(stderr,"Inimplementated case\n");
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(fullfilename);
		return r;
	}
	r = md5sum_and_size(md5sum,fullfilename,0);
	free(fullfilename);
	if( RET_WAS_ERROR(r) )
		return r;

	return RET_OK;
}
