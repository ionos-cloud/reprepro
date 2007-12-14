/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2006 Bernhard R. Link
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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "md5sum.h"
#include "copyfile.h"

extern int verbose;

retvalue copy(const char *fullfilename,const char *origfile,/*@null@*/const char *md5expected,/*@null@*//*@out@*/char **calculatedmd5sum) {
	char *md5sum;
	retvalue r;

	r = md5sum_copy(origfile,fullfilename,&md5sum);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Could not open '%s'!\n",origfile);
		return RET_ERROR_MISSING;
	}
	if( r == RET_ERROR_EXIST ) {
		fprintf(stderr,"File '%s' does already exist!\n",fullfilename);
	}
	if( RET_WAS_ERROR(r) )
		return r;

	if( md5expected != NULL ) {
		if( strcmp(md5sum,md5expected) == 0 ) {
			r = RET_OK;
		} else {
			(void)unlink(fullfilename);
			fprintf(stderr,"WARNING: '%s' has md5sum '%s', while '%s' was expected.\n",origfile,md5sum,md5expected);
			r = RET_ERROR_WRONG_MD5;
		}
	}

	if( calculatedmd5sum != NULL )
		*calculatedmd5sum = md5sum;
	else
		free(md5sum);

	return r;
}

static retvalue move(const char *fullfilename,const char *origfile,/*@null@*/const char *md5expected,/*@out@*/char **md5sum) {
	retvalue r;
	// TODO: try a rename first, if md5sum is know and correct??

	r = copy(fullfilename,origfile,md5expected,md5sum);
	if( RET_IS_OK(r) ) {
		if( verbose > 15 ) {
			fprintf(stderr,"Deleting '%s' after copying away.\n",origfile);
		}
		if( unlink(origfile) != 0 ) {
			fprintf(stderr,"Error deleting '%s': %m",origfile);
		}
	}
	return r;
}

retvalue copyfile_move(const char *mirrordir,const char *filekey,const char *origfile,const char *md5expected,char **md5sum) {
	retvalue r;
	char *fullfilename;

	fullfilename = calc_dirconcat(mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	r = move(fullfilename,origfile,md5expected,md5sum);
	free(fullfilename);
	return r;
}
retvalue copyfile_copy(const char *mirrordir,const char *filekey,const char *origfile,const char *md5expected,char **md5sum) {
	retvalue r;
	char *fullfilename;

	fullfilename = calc_dirconcat(mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	r = copy(fullfilename,origfile,md5expected,md5sum);
	free(fullfilename);
	return r;
}

retvalue copyfile_hardlink(const char *mirrordir, const char *filekey, const char *tempfile, const char *md5sum) {
	retvalue r;
	int i,e;
	char *fullfilename = calc_fullfilename(mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;

	i = link(tempfile, fullfilename);
	e = errno;
	if( i != 0 && e == EEXIST )  {
		unlink(fullfilename);
		i = link(tempfile, fullfilename);
		e = errno;
	}
	if( i != 0 && ( e == EACCES || e == ENOENT || e == ENOTDIR ) )  {
		dirs_make_parent(fullfilename);
		i = link(tempfile, fullfilename);
		e = errno;
	}
	if( i != 0 ) {
		if( e == EXDEV || e == EPERM || e == EMLINK ) {
			r = copy(fullfilename, tempfile, md5sum, NULL);
			if( RET_WAS_ERROR(r) ) {
				free(fullfilename);
				return r;
			}
		} else {
			fprintf(stderr,
"Error creating hardlink of '%s' as '%s': %d=%s\n",
					tempfile, fullfilename, e, strerror(e));
			free(fullfilename);
			return RET_ERRNO(e);
		}
	}

	free(fullfilename);
	return RET_OK;
}
