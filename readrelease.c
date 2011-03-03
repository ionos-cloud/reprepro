/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "error.h"
#include "names.h"
#include "chunks.h"
#include "readrelease.h"

/* get a strlist with the md5sums of a Release-file */
retvalue release_getchecksums(const char *releasefile,struct strlist *info) {
	gzFile fi;
	retvalue r;
	char *chunk;
	struct strlist files,checksuminfo;
	char *filename,*md5sum;
	int i;

	fi = gzopen(releasefile,"r");
	if( fi == NULL ) {
		fprintf(stderr,"Error opening %s: %m!\n",releasefile);
		return RET_ERRNO(errno);
	}
	r = chunk_read(fi,&chunk);
	i = gzclose(fi);
	if( !RET_IS_OK(r) ) {
		fprintf(stderr,"Error reading %s.\n",releasefile);
		if( r == RET_NOTHING )
			return RET_ERROR;
		else
			return r;
	}
	if( i < 0) {
		fprintf(stderr,"Closing revealed reading error in %s.\n",releasefile);
		free(chunk);
		return RET_ZERRNO(i);
	}
	r = chunk_getextralinelist(chunk,"MD5Sum",&files);
	free(chunk);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing MD5Sum-field.\n");
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;

	r = strlist_init(&checksuminfo);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&files);
		return r;
	}

	for( i = 0 ; i < files.count ; i++ ) {
		r = calc_parsefileline(files.values[i],&filename,&md5sum);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(&checksuminfo);
			return r;
		}
		r = strlist_add(&checksuminfo,filename);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(&checksuminfo);
			return r;
		}
		r = strlist_add(&checksuminfo,md5sum);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(&checksuminfo);
			return r;
		}
	}
	strlist_done(&files);
	assert( checksuminfo.count % 2 == 0 );

	strlist_move(info,&checksuminfo);
	return RET_OK;
}
