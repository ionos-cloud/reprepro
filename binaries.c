/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004 Bernhard R. Link
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
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "binaries.h"
#include "names.h"
#include "dpkgversions.h"

extern int verbose;

/* get md5sums out of a "Packages.gz"-chunk. */
static retvalue binaries_parse_md5sum(const char *chunk,struct strlist *md5sums) {
	retvalue r;
	/* collect the given md5sum and size */

	char *pmd5,*psize,*md5sum;

	r = chunk_getvalue(chunk,"MD5sum",&pmd5);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'MD5sum'-line in binary control chunk:\n '%s'\n",chunk);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	r = chunk_getvalue(chunk,"Size",&psize);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Size'-line in binary control chunk:\n '%s'\n",chunk);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		free(pmd5);
		return r;
	}
	md5sum = calc_concatmd5andsize(pmd5,psize);
	free(pmd5);free(psize);
	if( md5sum == NULL ) {
		return RET_ERROR_OOM;
	}
	r = strlist_init_singleton(md5sum,md5sums);
	if( RET_WAS_ERROR(r) ) {
		free(md5sum);
		return r;
	}
	return RET_OK;
}

/* get somefields out of a "Packages.gz"-chunk. returns RET_OK on success, RET_NOTHING if incomplete, error otherwise */
static retvalue binaries_parse_chunk(const char *chunk,const char *packagename,const char *suffix,const char *version,char **sourcename,char **basename) {
	retvalue r;
	char *parch;
	char *mysourcename,*mybasename;

	assert(packagename);

	/* get the sourcename */
	r = chunk_getname(chunk,"Source",&mysourcename,TRUE);
	if( r == RET_NOTHING ) {
		mysourcename = strdup(packagename);
		if( !mysourcename )
			r = RET_ERROR_OOM;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	/* generate a base filename based on package,version and architecture */
	r = chunk_getvalue(chunk,"Architecture",&parch);
	if( !RET_IS_OK(r) ) {
		free(mysourcename);
		return r;
	}
	r = properpackagename(packagename);
	if( !RET_WAS_ERROR(r) )
		r = propername(version);
	if( !RET_WAS_ERROR(r) )
		r = propername(parch);
	if( RET_WAS_ERROR(r) ) {
		free(parch);
		return r;
	}
	mybasename = calc_binary_basename(packagename,version,parch,suffix);
	free(parch);
	if( !mybasename ) {
		free(mysourcename);
		return RET_ERROR_OOM;
	}

	*basename = mybasename;
	*sourcename = mysourcename;
	return RET_OK;
}

/* get files out of a "Packages.gz"-chunk. */
static retvalue binaries_parse_getfilekeys(const char *chunk,struct strlist *files) {
	retvalue r;
	char *filename;
	
	/* Read the filename given there */
	r = chunk_getvalue(chunk,"Filename",&filename);
	if( !RET_IS_OK(r) ) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"Does not look like binary control: '%s'\n",chunk);
			r = RET_ERROR;
		}
		return r;
	}
	r = strlist_init_singleton(filename,files);
	if( !RET_IS_OK(r) )
		free(filename);
	return r;
}

retvalue binaries_calcfilekeys(const char *component,const char *sourcename,const char *basename,struct strlist *filekeys) {
	char *filekey;
	retvalue r;

	r = propername(sourcename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	filekey =  calc_filekey(component,sourcename,basename);
	if( !filekey )
		return RET_ERROR_OOM;
	r = strlist_init_singleton(filekey,filekeys);
	if( RET_WAS_ERROR(r) ) {
		free(filekey);
	}
	return r;
}

static inline retvalue calcnewcontrol(const char *chunk,const char *sourcename,const char *basename,const char *component,struct strlist *filekeys,char **newchunk) {
	retvalue r;

	r = binaries_calcfilekeys(component,sourcename,basename,filekeys);
	if( RET_WAS_ERROR(r) )
		return r;

	assert( filekeys->count == 1 );
	*newchunk = chunk_replacefield(chunk,"Filename",filekeys->values[0]);
	if( !*newchunk ) {
		strlist_done(filekeys);
		return RET_ERROR_OOM;
	}
	return RET_OK;
}

retvalue binaries_getname(struct target *t,const char *control,char **packagename){
	retvalue r;

	r = chunk_getvalue(control,"Package",packagename);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not found Package name in chunk:'%s'\n",control);
		return RET_ERROR;
	}
	return r;
}
retvalue binaries_getversion(struct target *t,const char *control,char **version) {
	retvalue r;

	r = chunk_getvalue(control,"Version",version);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not found Version in chunk:'%s'\n",control);
		return RET_ERROR;
	}
	return r;
}
	
retvalue binaries_getinstalldata(struct target *t,const char *packagename,const char *version,const char *chunk,char **control,struct strlist *filekeys,struct strlist *md5sums,struct strlist *origfiles) {
	char *sourcename,*basename;
	struct strlist mymd5sums;
	retvalue r;

	r = binaries_parse_md5sum(chunk,&mymd5sums);
	if( RET_WAS_ERROR(r) )
		return r;
	r = binaries_parse_chunk(chunk,packagename,t->suffix,version,&sourcename,&basename);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&mymd5sums);
		return r;
	} else if( r == RET_NOTHING ) {
		fprintf(stderr,"Does not look like a binary package: '%s'!\n",chunk);
		return RET_ERROR;
	}
	r = binaries_parse_getfilekeys(chunk,origfiles);
	if( RET_WAS_ERROR(r) ) {
		free(sourcename);free(basename);
		strlist_done(&mymd5sums);
		return r;
	}

	r = calcnewcontrol(chunk,sourcename,basename,t->component,filekeys,control);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&mymd5sums);
	} else {
		assert( r != RET_NOTHING );
		strlist_move(md5sums,&mymd5sums);
	}
	free(sourcename);free(basename);
	return r;
}

retvalue binaries_getfilekeys(struct target *t,const char *chunk,struct strlist *filekeys,struct strlist *md5sums) {
	retvalue r;
	r = binaries_parse_getfilekeys(chunk,filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	if( md5sums == NULL )
		return r;
	r = binaries_parse_md5sum(chunk,md5sums);
	return r;
}
char *binaries_getupstreamindex(struct target *target,const char *suite_from,
		const char *component_from,const char *architecture) {
	return mprintf("dists/%s/%s/binary-%s/Packages.gz",suite_from,component_from,architecture);
}
char *ubinaries_getupstreamindex(struct target *target,const char *suite_from,
		const char *component_from,const char *architecture) {
	return mprintf("dists/%s/%s/debian-installer/binary-%s/Packages.gz",suite_from,component_from,architecture);
}
