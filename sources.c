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

#include <assert.h>
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
#include "mprintf.h"
#include "dpkgversions.h"

extern int verbose;

/* traverse through a '\n' sepeated lit of "<md5sum> <size> <filename>" 
 * > 0 while entires found, ==0 when not, <0 on error */
retvalue sources_getfile(const char *fileline,char **basename,char **md5andsize) {
	const char *md5,*md5end,*size,*sizeend,*fn,*fnend;
	char *md5as,*filen;

	assert( fileline != NULL );
	if( *fileline == '\0' )
		return RET_NOTHING;

	/* the md5sums begins after the (perhaps) heading spaces ...  */
	md5 = fileline;
	while( isspace(*md5) )
		md5++;
	if( *md5 == '\0' )
		return RET_NOTHING;

	/* ... and ends with the following spaces. */
	md5end = md5;
	while( *md5end != '\0' && !isspace(*md5end) )
		md5end++;
	if( !isspace(*md5end) ) {
		if( verbose >= 0 ) {
			fprintf(stderr,"Expecting more data after md5sum!\n");
		}
		return RET_ERROR;
	}
	/* Then the size of the file is expected: */
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
	/* Then the filename */
	fn = sizeend;
	while( isspace(*fn) )
		fn++;
	fnend = fn;
	while( *fnend != '\0' && !isspace(*fnend) )
		fnend++;

	filen = strndup(fn,fnend-fn);
	if( !filen )
		return RET_ERROR_OOM;
	if( md5andsize ) {
		md5as = malloc((md5end-md5)+2+(sizeend-size));
		if( !md5as ) {
			free(filen);
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
retvalue sources_parse_chunk(const char *chunk,char **packagename,char **version,char **origdirectory,struct strlist *files) {
	retvalue r;
#define IFREE(p) if(p) free(*p);

	if( packagename ) {
		r = chunk_getvalue(chunk,"Package",packagename);
		if( !RET_IS_OK(r) )
			return r;
	}
	
	if( version ) {
		r = chunk_getvalue(chunk,"Version",version);
		if( !RET_IS_OK(r) ) {
			IFREE(packagename);
			return r;
		}
	}

	if( origdirectory ) {
		/* Read the directory given there */
		r = chunk_getvalue(chunk,"Directory",origdirectory);
		if( !RET_IS_OK(r) ) {
			IFREE(packagename);
			IFREE(version);
			return r;
		}
		if( verbose > 13 ) 
			fprintf(stderr,"got: %s\n",*origdirectory);
	}


	/* collect the given md5sum and size */

  	if( files ) {
  
		r = chunk_getextralinelist(chunk,"Files",files);
		if( !RET_IS_OK(r) ) {
			IFREE(packagename);
			IFREE(version);
  			IFREE(origdirectory);
  			return r;
		}
	}

	return RET_OK;
}

/* compare versions, 1= new is better, 0=old is better, <0 error */
static int sources_isnewer(const char *newchunk,const char *oldchunk) {
	char *nv,*ov;
	int r;
	retvalue ret;

	/* if new broken, tell it, if old broken, new is better: */
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

//typedef retvalue source_package_action(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,const char *oldchunk);

struct sources_add {DB *pkgs; void *data; const char *component; source_package_action *action; };

static retvalue addsource(void *data,const char *chunk) {
	retvalue r;
	int isnewer;
	struct sources_add *d = data;

	char *package,*version,*directory,*olddirectory;
	char *oldchunk;
	struct strlist files;

	r = sources_parse_chunk(chunk,&package,&version,&olddirectory,&files);
	if( r == RET_NOTHING ) {
		return RET_ERROR;
	} else if( RET_WAS_ERROR(r) ) {
		return r;
	}
	oldchunk = packages_get(d->pkgs,package);
	if( oldchunk && (isnewer=sources_isnewer(chunk,oldchunk)) != 0 ) {
		if( isnewer < 0 ) {
			fprintf(stderr,"Omitting %s because of parse errors.\n",package);
			free(package);strlist_done(&files);
			free(olddirectory);free(version);
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
			r = (*d->action)(d->data,chunk,package,version,directory,olddirectory,&files,oldchunk);
		free(directory);
	} else {
		r = RET_NOTHING;
	}
	free(oldchunk);
	
	free(package);strlist_done(&files);
	free(olddirectory);free(version);

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
	char *directory;
	struct strlist files;
	char *filename,*filekey;
	char *package,*version,*identifier;
	retvalue r,result;
	int i;

	r = sources_parse_chunk(chunk,&package,&version,&directory,&files);
	if( !RET_IS_OK(r) )
		return r;

	identifier = mprintf("%s %s %s",referee,package,version);
	free(version);free(package);
	if( !identifier ) {
		free(directory);strlist_done(&files);
		return RET_ERROR_OOM;
	}

	result = RET_NOTHING;
	
	for( i = 0 ; i < files.count ; i++ ) {
		r = sources_getfile(files.values[i],&filename,NULL);
		if( RET_WAS_ERROR(r) ) {
			result = r;
			break;
		}
		filekey = calc_srcfilekey(directory,filename);
		if( verbose > 4 ) {
			fprintf(stderr,"Decrementing reference for '%s' to '%s'...\n",referee,filekey);
		}

		r = references_decrement(refs,filekey,identifier);
		RET_UPDATE(result,r);

		free(filename);free(filekey);
	}
	free(identifier);
	free(directory);strlist_done(&files);

	return result;
}

/* Add references for the given source */
retvalue sources_reference(DB *refs,const char *referee,const char *package,const char *version,const char *dir,const struct strlist *files) {
	char *basefilename,*filekey;
	char *identifier;
	retvalue r,result;
	int i;

	identifier = mprintf("%s %s %s",referee,package,version);
	if( !identifier ) {
		return RET_ERROR_OOM;
	}

	result = RET_NOTHING;
	for( i = 0 ; i < files->count ; i++ ) {
		r = sources_getfile(files->values[i],&basefilename,NULL);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			break;
		}
		filekey = calc_srcfilekey(dir,basefilename);
		free(basefilename);
		if( !filekey) {
			free(identifier);
			return RET_ERROR_OOM;
		}
		r = references_increment(refs,filekey,identifier);
		free(filekey);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	free(identifier);
	return result;
}
