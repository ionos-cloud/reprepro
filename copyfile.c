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
#include "copyfile.h"


//TODO: split this in parts: one static part that copy directly,
// one part doing the mirrordir-calculation and calculates the md5sum
// and one part which gets a md5sum and checks for the generated file
// to have this one.

retvalue copyfile(const char *mirrordir,const char *filekey,const char *origfile) {
	char *destfullfilename;
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

	destfullfilename = calc_fullfilename(mirrordir,filekey);
	if( !destfullfilename ) {
		close(fd);
		return RET_ERROR_OOM;
	}

	r = dirs_make_parent(destfullfilename);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error creating '%s': %m\n",destfullfilename);
		free(destfullfilename);
		close(fd);
		return r;

	}

	fdw = open(destfullfilename,O_NOCTTY|O_WRONLY|O_CREAT|O_EXCL,0777);
	if( fdw < 0 ) {
		ret = errno;
		fprintf(stderr,"Error creating '%s': %m\n",destfullfilename);
		free(destfullfilename);
		close(fd);
		return RET_ERRNO(ret);
	}

	written = write(fdw,content,stat.st_size);
	if( written < stat.st_size ) {
		ret = errno;
		fprintf(stderr,"Error while writing to '%s': %m\n",destfullfilename);
		close(fd);
		close(fdw);
		unlink(destfullfilename);
		free(destfullfilename);
		return RET_ERRNO(ret);
	}

	ret = close(fdw);
	if( ret < 0 ) {
		ret = errno;
		fprintf(stderr,"Error writing to '%s': %m\n",destfullfilename);
		close(fd);
		unlink(destfullfilename);
		free(destfullfilename);
		return RET_ERRNO(ret);
	}
	free(destfullfilename);

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


