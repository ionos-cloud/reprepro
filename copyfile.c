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

/* Copy a file and calculate the md5sum of the result,
 * return RET_NOTHING (and no md5sum), if it already exists*/
static retvalue copyfile(const char *origfile,const char *destfullfilename,char **md5sum) {
	retvalue r;

	r = md5sum_copy(origfile,destfullfilename,md5sum);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Error opening '%s'!\n",origfile);
		r = RET_ERROR;
	}
	if( r == RET_ERROR_EXIST )
		r = RET_NOTHING;
	return r;
}

/* fullfilename is already there, but we want it do be md5expected, try
 * to get there by copying origfile, if it has the wrong md5sum... */
static retvalue copyfile_force(const char *fullfilename,const char *origfile,const char *md5expected) {
	char *md5old;
	retvalue r;

	r = md5sum_read(fullfilename,&md5old);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Strange file '%s'!\n",fullfilename);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	if( strcmp(md5old,md5expected) != 0 ) {
		int ret;

		/* There already is a file there, as
		 * we should have queried the db, this
		 * means this is a leftover file, we
		 * just delete and try again... */
		fprintf(stderr,"There already is a non-registered file '%s' with the wrong md5sum ('%s', but expect '%s'). Removing it...\n",fullfilename,md5old,md5expected);
		free(md5old);
		ret = unlink(fullfilename);
		if( ret < 0 ) {
			ret = errno;
			fprintf(stderr,"Error deleting '%s': %m\n",fullfilename);
			return RET_ERRNO(ret);
		}
		/* try again: */
		r = copyfile(origfile,fullfilename,&md5old);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	if( strcmp(md5old,md5expected) != 0 ) {
		unlink(fullfilename);
		fprintf(stderr,"'%s' has md5sum '%s', while '%s' was expected.\n",origfile,md5old,md5expected);
		r = RET_ERROR_WRONG_MD5;
	}
	free(md5old);
	return r;
}

/* Make sure <mirrordir>/<filekey> has <md5expected>, and we can get
 * there by copying <origfile> there. */
retvalue copyfile_md5known(const char *mirrordir,const char *filekey,const char *origfile,const char *md5expected) {
	retvalue r;
	char *fullfilename;
	char *md5sum;

	fullfilename = calc_dirconcat(mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;

	r = copyfile(origfile,fullfilename,&md5sum);
	if( r == RET_ERROR_EXIST ) {
		r = copyfile_force(origfile,fullfilename,md5expected);
	} else if( RET_IS_OK(r) ) {
		if( strcmp(md5sum,md5expected) == 0 ) {
			r = RET_OK;
		} else {
			unlink(fullfilename);
			fprintf(stderr,"'%s' has md5sum '%s', while '%s' was expected.\n",origfile,md5sum,md5expected);
			r = RET_ERROR_WRONG_MD5;
		}
		free(md5sum);
	}
	free(fullfilename);
	return r;
}

retvalue copyfile_getmd5(const char *mirrordir,const char *filekey,const char *origfile,char **md5sum) {
	retvalue r;
	char *fullfilename;

	fullfilename = calc_dirconcat(mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;

	r = copyfile(origfile,fullfilename,md5sum);
	if( r == RET_ERROR_EXIST ) {
		r = md5sum_read(origfile,md5sum);
		if( r == RET_NOTHING ) {
			/* where did it go? */
			fprintf(stderr,"File '%s' disapeared!\n",fullfilename);
			r = RET_ERROR;
		}
		if( RET_IS_OK(r) )
			r = copyfile_force(origfile,fullfilename,*md5sum);
		if( RET_WAS_ERROR(r) ) {
			free(*md5sum);
			*md5sum = NULL;
			free(fullfilename);
			return r;
		}

	} 
	free(fullfilename);
	return r;
}
