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
#include "packages.h"
#include "chunks.h"
#include "binaries.h"
#include "names.h"
#include "dpkgversions.h"

extern int verbose;
extern int force;

/* get somefields out of a "Packages.gz"-chunk. returns 1 on success, 0 if incomplete, -1 on error */
static retvalue binaries_parse_chunk(const char *chunk,char **packagename,char **origfilename,char **sourcename,char **filename,char **md5andsize) {
	const char *f,*f2;
	char *pmd5,*psize,*ppackage;
#define IFREE(p) if(p) free(*p);

	f  = chunk_getfield("Package",chunk);
	if( !f ) {
		return RET_NOTHING;
	}
	ppackage = chunk_dupvalue(f);	
	if( !ppackage ) {
		return RET_ERROR_OOM;
	}
	if( packagename ) {
		*packagename = ppackage;
	}

	if( origfilename ) {
		/* Read the filename given there */
		f = chunk_getfield("Filename",chunk);
		if( ! f ) {
			free(ppackage);
			return RET_NOTHING;
		}
		*origfilename = chunk_dupvalue(f);
		if( !*origfilename ) {
			free(ppackage);
			return RET_ERROR_OOM;
		}
		if( verbose > 3 ) 
			fprintf(stderr,"got: %s\n",*origfilename);
	}


	/* collect the given md5sum and size */

	if( md5andsize ) {

		f = chunk_getfield("MD5sum",chunk);
		f2 = chunk_getfield("Size",chunk);
		if( !f || !f2 ) {
			free(ppackage);
			IFREE(origfilename);
			return RET_NOTHING;
		}
		pmd5 = chunk_dupvalue(f);
		psize = chunk_dupvalue(f2);
		if( !pmd5 || !psize ) {
			free(ppackage);
			free(psize);free(pmd5);
			IFREE(origfilename);
			return RET_ERROR_OOM;
		}
		asprintf(md5andsize,"%s %s",pmd5,psize);
		free(pmd5);free(psize);
		if( !*md5andsize ) {
			free(ppackage);
			IFREE(origfilename);
			return RET_ERROR_OOM;
		}
	}

	/* get the sourcename */

	if( sourcename ) {
		f  = chunk_getfield("Source",chunk);
		if( f )
			/* do something with the version here? */
			*sourcename = chunk_dupword(f);	
		else {
			*sourcename = strdup(ppackage);
		}
		if( !*sourcename ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			return RET_ERROR_OOM;
		}
	}

	/* generate a filename based on package,version and architecture */

	if( filename ) {
		char *parch,*pversion,*v;

		f  = chunk_getfield("Version",chunk);
		f2 = chunk_getfield("Architecture",chunk);
		if( !f || !f2 ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			return RET_NOTHING;
		}
		pversion = chunk_dupvalue(f);
		parch = chunk_dupvalue(f2);
		if( !parch || !pversion ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			return RET_ERROR_OOM;
		}
		v = index(pversion,':');
		if( v )
			v++;
		else
			v = pversion;
		/* TODO check parts to contain out of save charakters */
		*filename = calc_package_filename(ppackage,v,parch);
		if( !*filename ) {
			free(pversion);free(parch);
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			return RET_ERROR_OOM;
		}
		free(pversion);free(parch);
	}

	if( packagename == NULL)
		free(ppackage);

	return RET_OK;
}

/* check if one chunk describes a packages superseded by another
 * return 1=new is better, 0=old is better, <0 error */
static int binaries_isnewer(const char *newchunk,const char *oldchunk) {
	char *nv,*ov;
	int r;

	/* if new broken, old is better, if old broken, new is better: */
	nv = chunk_dupvalue(chunk_getfield("Version",newchunk));
	if( !nv )
		return -1;
	ov = chunk_dupvalue(chunk_getfield("Version",oldchunk));
	if( !ov ) {
		free(nv);
		return 1;
	}
	r = isVersionNewer(nv,ov);
	free(nv);free(ov);
	return r;
}


struct binaries_add {DB *pkgs; void *data; const char *part; binary_package_action *action; };

static retvalue addbinary(void *data,const char *chunk) {
	struct binaries_add *d = data;
	retvalue r;
	int newer,hadold;
	char *oldchunk;
	char *package,*filename,*oldfile,*sourcename,*filekey,*md5andsize;

	r = binaries_parse_chunk(chunk,&package,&oldfile,&sourcename,&filename,&md5andsize);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Cannot parse chunk: '%s'!\n",chunk);
		return r;
	} else if( r == RET_NOTHING ) {
		fprintf(stderr,"Does not look like a binary package: '%s'!\n",chunk);
		return RET_ERROR;
	}
	assert(RET_IS_OK(r));
	hadold = 0;
	oldchunk = packages_get(d->pkgs,package);
	if( oldchunk && (newer=binaries_isnewer(chunk,oldchunk)) != 0 ) {
		free(oldchunk);
		oldchunk = NULL;
		if( newer < 0 ) {
			fprintf(stderr,"Omitting %s because of parse errors.\n",package);
			free(md5andsize);free(oldfile);free(package);
			free(sourcename);free(filename);
			return RET_ERROR;
		}
		/* old package will be obsoleted */
		hadold=1;
	}
	if( oldchunk == NULL ) {
		/* add package (or whatever action wants to do) */

		filekey =  calc_filekey(d->part,sourcename,filename);
		if( !filekey )
			r = RET_ERROR_OOM;
		else {
			r = (*d->action)(d->data,chunk,
					package,sourcename,oldfile,filename,
					filekey,md5andsize,hadold);
			free(filekey);
		}

	} else {
		r = RET_NOTHING;
		free(oldchunk);
	}
	
	free(package);free(md5andsize);
	free(oldfile);free(filename);free(sourcename);
	return r;
}



/* call action for each package in packages_file */
retvalue binaries_add(DB *pkgs,const char *part,const char *packages_file, binary_package_action action,void *data) {
	struct binaries_add mydata;

	mydata.data=data;
	mydata.pkgs=pkgs;
	mydata.part=part;
	mydata.action=action;

	return chunk_foreach(packages_file,addbinary,&mydata,force);
}
