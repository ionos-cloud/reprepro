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
#include "mprintf.h"
#include "packages.h"
#include "chunks.h"
#include "binaries.h"
#include "names.h"
#include "dpkgversions.h"

extern int verbose;

/* get somefields out of a "Packages.gz"-chunk. returns 1 on success, 0 if incomplete, -1 on error */
retvalue binaries_parse_chunk(const char *chunk,char **packagename,char **origfilename,char **sourcename,char **basename,char **md5andsize) {
	char *pmd5,*psize,*ppackage;
	retvalue r;
#define IFREE(p) if(p) free(*p);

	r  = chunk_getvalue(chunk,"Package", &ppackage);
	if( !RET_IS_OK(r) )
		return r;
	if( !ppackage ) {
		return RET_ERROR_OOM;
	}
	if( packagename ) {
		*packagename = ppackage;
	}

	if( origfilename ) {
		/* Read the filename given there */
		r = chunk_getvalue(chunk,"Filename",origfilename);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			return r;
		}
	}


	/* collect the given md5sum and size */

	if( md5andsize ) {

		r = chunk_getvalue(chunk,"MD5sum",&pmd5);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			IFREE(origfilename);
			return r;
		}
		r = chunk_getvalue(chunk,"Size",&psize);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			IFREE(origfilename);
			free(pmd5);
			return r;
		}
		*md5andsize = mprintf("%s %s",pmd5,psize);
		free(pmd5);free(psize);
		if( !*md5andsize ) {
			free(ppackage);
			IFREE(origfilename);
			return RET_ERROR_OOM;
		}
	}

	/* get the sourcename */

	if( sourcename ) {
		r = chunk_getfirstword(chunk,"Source",sourcename);
		if( r == RET_NOTHING ) {
			*sourcename = strdup(ppackage);
			if( !*sourcename )
				r = RET_ERROR_OOM;
		}
		if( RET_WAS_ERROR(r) ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			return r;
		}
	}

	/* generate a base filename based on package,version and architecture */

	if( basename ) {
		char *parch,*pversion,*v;

		r = chunk_getvalue(chunk,"Version",&pversion);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			return r;
		}
		r = chunk_getvalue(chunk,"Architecture",&parch);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			free(pversion);
			return r;
		}
		v = index(pversion,':');
		if( v )
			v++;
		else
			v = pversion;
		/* TODO check parts to consist out of save charakters */
		*basename = calc_package_basename(ppackage,v,parch);
		free(pversion);free(parch);
		if( !*basename ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			return RET_ERROR_OOM;
		}
	}

	if( packagename == NULL)
		free(ppackage);

	return RET_OK;
}

/* check if one chunk describes a packages superseded by another
 * return 1=new is better, 0=old is better, <0 error */
static int binaries_isnewer(const char *newchunk,const char *oldchunk) {
	char *nv,*ov;
	retvalue ret;
	int r;

	/* if new broken, old is better, if old broken, new is better: */
	ret = chunk_getvalue(newchunk,"Version",&nv);
	if( !RET_IS_OK(ret) )
		return -1;
	ret = chunk_getvalue(oldchunk,"Version",&ov);
	if( !RET_IS_OK(ret) ) {
		free(nv);
		return 1;
	}
	r = isVersionNewer(nv,ov);
	free(nv);free(ov);
	return r;
}


struct binaries_add {DB *pkgs; void *data; const char *component; binary_package_action *action; };

static retvalue addbinary(void *data,const char *chunk) {
	struct binaries_add *d = data;
	retvalue r;
	int newer;
	char *oldchunk;
	char *package,*basename,*origfile,*sourcename,*filekey,*md5andsize;

	r = binaries_parse_chunk(chunk,&package,&origfile,&sourcename,&basename,&md5andsize);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Cannot parse chunk: '%s'!\n",chunk);
		return r;
	} else if( r == RET_NOTHING ) {
		fprintf(stderr,"Does not look like a binary package: '%s'!\n",chunk);
		return RET_ERROR;
	}
	assert(RET_IS_OK(r));
	oldchunk = packages_get(d->pkgs,package);
	if( oldchunk && (newer=binaries_isnewer(chunk,oldchunk)) != 0 ) {
		if( newer < 0 ) {
			fprintf(stderr,"Omitting %s because of parse errors.\n",package);
			free(md5andsize);free(origfile);free(package);
			free(sourcename);free(basename);
			free(oldchunk);
			return RET_ERROR;
		}
	}
	if( oldchunk==NULL || newer > 0 ) {
		/* add package (or whatever action wants to do) */

		filekey =  calc_filekey(d->component,sourcename,basename);
		if( !filekey )
			r = RET_ERROR_OOM;
		else {
			r = (*d->action)(d->data,chunk,
					package,sourcename,origfile,basename,
					filekey,md5andsize,oldchunk);
			free(filekey);
		}

	} else {
		r = RET_NOTHING;
	}
	free(oldchunk);
	
	free(package);free(md5andsize);
	free(origfile);free(basename);free(sourcename);
	return r;
}



/* call action for each package in packages_file */
retvalue binaries_add(DB *pkgs,const char *component,const char *packages_file, binary_package_action action,void *data,int force) {
	struct binaries_add mydata;

	mydata.data=data;
	mydata.pkgs=pkgs;
	mydata.component=component;
	mydata.action=action;

	return chunk_foreach(packages_file,addbinary,&mydata,force,0);
}
