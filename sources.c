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
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include "error.h"
#include "packages.h"
#include "reference.h"
#include "chunks.h"
#include "sources.h"
#include "names.h"
#include "dpkgversions.h"

extern int verbose;

/* traverse through a '\n' sepeated lit of "<md5sum> <size> <filename>" 
 * > 0 while entires found, ==0 when not, <0 on error */
retvalue sources_getfile(const char **files,char **basename,char **md5andsize) {
	const char *md5,*md5end,*size,*sizeend,*fn,*fnend,*p;
	char *md5as,*filen;

	if( !files || !*files || !**files )
		return RET_NOTHING;

	md5 = *files;
	while( isspace(*md5) )
		md5++;
	md5end = md5;
	while( *md5end && !isspace(*md5end) )
		md5end++;
	if( !isspace(*md5end) ) {
		if( verbose >= 0 ) {
			fprintf(stderr,"Expecting more data after md5sum!\n");
		}
		return RET_ERROR;
	}
	size = md5end;
	while( isspace(*size) )
		size++;
	sizeend = size;
	while( isdigit(*sizeend) )
		sizeend++;
	if( !isspace(*sizeend) ) {
		if( verbose >= 0 ) {
			fprintf(stderr,"Error in parsing size or missing space afterwards!\n");
		}
		return RET_ERROR;
	}
	fn = sizeend;
	while( isspace(*fn) )
		fn++;
	fnend = fn;
	while( *fnend && !isspace(*fnend) )
		fnend++;
	if( *fnend && !isspace(*fnend) )
		return RET_ERROR;
	p = fnend;
	while( *p && *p != '\n' )
		p++;
	if( *p )
		p++;
	*files = p;

	filen = strndup(fn,fnend-fn);
	if( md5andsize ) {
		md5as = malloc((md5end-md5)+2+(sizeend-size));
		if( !filen || !md5as ) {
			free(filen);free(md5as);
			return RET_ERROR_OOM;
		}
		strncpy(md5as,md5,md5end-md5);
		md5as[md5end-md5] = ' ';
		strncpy(md5as+1+(md5end-md5),size,sizeend-size);
		md5as[(md5end-md5)+1+(sizeend-size)] = '\0';
	
		*md5andsize = md5as;
	}
	if( basename )
		*basename = filen;
	else
		free(filen);

//	fprintf(stderr,"'%s' -> '%s' \n",*filename,*md5andsize);
	
	return RET_OK;
}

/* get the intresting information out of a "Sources.gz"-chunk */
retvalue sources_parse_chunk(const char *chunk,char **packagename,char **origdirectory,char **files) {
	const char *f;
#define IFREE(p) if(p) free(*p);

	if( packagename ) {
		f  = chunk_getfield("Package",chunk);
		if( !f ) {
			if( verbose > 3 )
				fprintf(stderr,"Source-Chunk without Package-field!\n");
			return RET_NOTHING;
		}
		*packagename = chunk_dupvalue(f);	
		if( !*packagename ) {
			return RET_ERROR;
		}
	}

	if( origdirectory ) {
		/* Read the directory given there */
		f = chunk_getfield("Directory",chunk);
		if( ! f ) {
			IFREE(packagename);
			return RET_NOTHING;
		}
		*origdirectory = chunk_dupvalue(f);
		if( !*origdirectory ) {
			IFREE(packagename);
			return RET_ERROR;
		}
		if( verbose > 13 ) 
			fprintf(stderr,"got: %s\n",*origdirectory);
	}


	/* collect the given md5sum and size */

  	if( files ) {
  
  		f = chunk_getfield("Files",chunk);
  		if( !f ) {
			IFREE(packagename);
  			IFREE(origdirectory);
  			return RET_NOTHING;
		}
		*files = chunk_dupextralines(f);
		if( !*files ) {
			IFREE(packagename);
			IFREE(origdirectory);
			return RET_ERROR;
		}
	}

	return RET_OK;
}

/* compare versions, 1= new is better, 0=old is better, <0 error */
static int sources_isnewer(const char *newchunk,const char *oldchunk) {
	char *nv,*ov;
	int r;

	/* if new broken, tell it, if old broken, new is better: */
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

//typedef retvalue source_package_action(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,const char *oldchunk);

struct sources_add {DB *pkgs; void *data; const char *component; source_package_action *action; };

static retvalue addsource(void *data,const char *chunk) {
	retvalue r;
	int isnewer;
	struct sources_add *d = data;

	char *package,*directory,*olddirectory,*files;
	char *oldchunk;

	r = sources_parse_chunk(chunk,&package,&olddirectory,&files);
	if( r == RET_NOTHING ) {
		return RET_ERROR;
	} else if( RET_WAS_ERROR(r) ) {
		return r;
	}
	oldchunk = packages_get(d->pkgs,package);
	if( oldchunk && (isnewer=sources_isnewer(chunk,oldchunk)) != 0 ) {
		if( isnewer < 0 ) {
			fprintf(stderr,"Omitting %s because of parse errors.\n",package);
			free(package);free(files);
			free(olddirectory);
			free(oldchunk);
			return RET_ERROR;
		}
	}
	if( oldchunk == NULL || isnewer > 0 ) {
		/* add source package */
		directory =  calc_sourcedir(d->component,package);
		if( !directory )
			r = RET_ERROR_OOM;
		else
			r = (*d->action)(d->data,chunk,package,directory,olddirectory,files,oldchunk);
		free(directory);
	} else {
		r = RET_NOTHING;
	}
	free(oldchunk);
	
	free(package);free(files);
	free(olddirectory);

	return r;
}

/* call <data> for each package in the "Sources.gz"-style file <source_file> missing in
 * <pkgs> and using <component> as subdir of pool (i.e. "main","contrib",...) for generated paths */
retvalue sources_add(DB *pkgs,const char *component,const char *sources_file, source_package_action action,void *data,int force) {
	struct sources_add mydata;

	mydata.data=data;
	mydata.pkgs=pkgs;
	mydata.component=component;
	mydata.action=action;

	return chunk_foreach(sources_file,addsource,&mydata,force);
}

/* remove all references by the given chunk */
retvalue sources_dereference(DB *refs,const char *referee,const char *chunk) {
	char *directory,*files;
	const char *nextfile;
	char *filename,*filekey;
	retvalue r,result;

	r = sources_parse_chunk(chunk,NULL,&directory,&files);
	if( !RET_IS_OK(r) )
		return r;

	result = RET_NOTHING;
	
	nextfile = files;
	while( RET_IS_OK(r=sources_getfile(&nextfile,&filename,NULL)) ){
		filekey = calc_srcfilekey(directory,filename);
		if( verbose > 4 ) {
			fprintf(stderr,"Decrementing reference for '%s' to '%s'...\n",referee,filekey);
		}

		r = references_decrement(refs,filekey,referee);
		RET_UPDATE(result,r);

		free(filename);free(filekey);
	}
	free(directory);free(files);

	return result;
}
