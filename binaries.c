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
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <malloc.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "packages.h"
#include "binaries.h"
#include "names.h"
#include "dpkgversions.h"

extern int verbose;

/* get somefields out of a "Packages.gz"-chunk. returns 1 on success, 0 if incomplete, -1 on error */
static retvalue binaries_parse_chunk(const char *chunk,const char *packagename,char **sourcename,char **basename,struct strlist *md5sums) {
	retvalue r;
#define IFREE(p) if(p) free(*p);
#define ISFREE(p) if(p) strlist_done(p);

	/* collect the given md5sum and size */

	if( md5sums ) {
		char *pmd5,*psize,*md5sum;

		r = chunk_getvalue(chunk,"MD5sum",&pmd5);
		if( !RET_IS_OK(r) ) {
			return r;
		}
		r = chunk_getvalue(chunk,"Size",&psize);
		if( !RET_IS_OK(r) ) {
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
	}

	/* get the sourcename */

	if( sourcename ) {
		r = chunk_getname(chunk,"Source",sourcename,1);
		if( r == RET_NOTHING ) {
			*sourcename = strdup(packagename);
			if( !*sourcename )
				r = RET_ERROR_OOM;
		}
		if( RET_WAS_ERROR(r) ) {
			ISFREE(md5sums);
			return r;
		}
	}

	/* generate a base filename based on package,version and architecture */

	if( basename ) {
		char *parch,*pversion;

		// TODO combine the two looks for version...
		r = chunk_getvalue(chunk,"Version",&pversion);
		if( !RET_IS_OK(r) ) {
			ISFREE(md5sums);
			IFREE(sourcename);
			return r;
		}
		r = chunk_getvalue(chunk,"Architecture",&parch);
		if( !RET_IS_OK(r) ) {
			ISFREE(md5sums);
			IFREE(sourcename);
			free(pversion);
			return r;
		}
		/* TODO check parts to consist out of save charakters */
		*basename = calc_binary_basename(packagename,pversion,parch);
		free(pversion);free(parch);
		if( !*basename ) {
			ISFREE(md5sums);
			IFREE(sourcename);
			return RET_ERROR_OOM;
		}
	}

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
	retvalue r;

	r = binaries_parse_chunk(chunk,packagename,&sourcename,&basename,md5sums);
	if( RET_WAS_ERROR(r) ) {
		return r;
	} else if( r == RET_NOTHING ) {
		fprintf(stderr,"Does not look like a binary package: '%s'!\n",chunk);
		return RET_ERROR;
	}
	r = binaries_parse_getfilekeys(chunk,origfiles);
	if( RET_WAS_ERROR(r) ) {
		free(sourcename);free(basename);
		strlist_done(md5sums);
		return r;
	}

	r = calcnewcontrol(chunk,sourcename,basename,t->component,filekeys,control);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(md5sums);
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
	r = binaries_parse_chunk(chunk,NULL,NULL,NULL,md5sums);
	return r;
}
