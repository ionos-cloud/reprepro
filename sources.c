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
#include "strlist.h"
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

/* Look for an older version of the Package in the database.
 * Set *oldversion, if there is already a newer (or equal) version to
 * <version> and <version> is != NULL */
retvalue sources_lookforolder(
		DB *packages,const char *packagename,
		const char *newversion,char **oldversion,
		char **olddirectory,struct strlist *oldfiles) {
	char *oldchunk,*ov;
	retvalue r;

	// TODO: why does packages_get return something else than a retvalue?
	oldchunk = packages_get(packages,packagename);
	if( oldchunk  == NULL ) {
		*olddirectory = NULL;
		if( oldversion != NULL && newversion != NULL )
			*oldversion = NULL;
		return RET_NOTHING;
	}

	if( newversion ) {
		assert(oldversion != NULL);
		r = sources_parse_chunk(oldchunk,NULL,&ov,olddirectory,oldfiles);
	} else {
		assert( oldversion == NULL );
		r = sources_parse_chunk(oldchunk,NULL,NULL,olddirectory,oldfiles);
	}

	if( !RET_IS_OK(r) ) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"Does not look like source control: '%s'\n",oldchunk);
			r = RET_ERROR;

		}
		free(oldchunk);
		return r;
	}

	if( newversion ) {
		r = dpkgversions_isNewer(newversion,ov);

		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr,"Parse errors processing versions of %s.\n",packagename);
			free(ov);
			free(*olddirectory);
			*olddirectory = NULL;
			strlist_done(oldfiles);
			free(oldchunk);
			return r;
		}
		if( RET_IS_OK(r) ) {
			*oldversion = NULL;
			free(ov);
		} else
			*oldversion = ov;
	}

	free(oldchunk);
	return r;
}

//typedef retvalue source_package_action(void *data,const char *chunk,const char *package,const char *directory,const char *origdirectory,const char *files,const char *oldchunk);

struct sources_add {DB *pkgs; void *data; const char *component; source_package_action *action; };

static retvalue addsource(void *data,const char *chunk) {
	retvalue r;
	struct sources_add *d = data;

	char *package,*version,*directory,*origdirectory;
	char *oldversion,*olddirectory;
	struct strlist files,oldfiles;

	r = sources_parse_chunk(chunk,&package,&version,&origdirectory,&files);
	if( r == RET_NOTHING ) {
		// TODO: error?
		return RET_ERROR;
	} else if( RET_WAS_ERROR(r) ) {
		return r;
	}
	r = sources_lookforolder(d->pkgs,package,version,&oldversion,&olddirectory,&oldfiles);
	if( RET_WAS_ERROR(r) ) {
		free(version);free(origdirectory);strlist_done(&files);
		return r;
	}

	if( oldversion != NULL ) {
		if( verbose > 40 )
			fprintf(stderr,"Ignoring '%s' with version '%s', as '%s'
					is already there.\n",package,version,oldversion);
		free(oldversion);
		r = RET_NOTHING;
	} else {
		/* add source package */
		directory =  calc_sourcedir(d->component,package);
		if( !directory )
			r = RET_ERROR_OOM;
		else 
			r = (*d->action)(d->data,chunk,package,version,directory,origdirectory,&files,olddirectory,&oldfiles);
		free(directory);
	}
	free(olddirectory);strlist_done(&oldfiles);
	free(package);strlist_done(&files);
	free(origdirectory);free(version);

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

	return chunk_foreach(sources_file,addsource,&mydata,force,0);
}

retvalue sources_getfilekeys(const char *directory,const struct strlist *files,struct strlist *filekeys) {
	int i;
	retvalue r;

	assert(directory != NULL && files != NULL);

	r = strlist_init_n(files->count,filekeys);
	if( RET_WAS_ERROR(r) )
		return r;

	for( i = 0 ; i < files->count ; i++ ) {
		char *filekey;

		filekey = calc_srcfilekey(directory,files->values[i]);
		if( filekey == NULL ) {
			strlist_done(filekeys);
			return RET_ERROR_OOM;
		}
		strlist_add(filekeys,filekey);
	}
	assert( files->count == filekeys->count );
	return RET_OK;
}

/* remove all references by the given chunk */
retvalue sources_dereference(DB *refs,const char *referee,const char *chunk) {
	char *directory;
	struct strlist files,filekeys;
	retvalue r;

	r = sources_parse_chunk(chunk,NULL,NULL,&directory,&files);
	if( !RET_IS_OK(r) )
		return r;

	r = sources_getfilekeys(directory,&files,&filekeys);
	free(directory);strlist_done(&filekeys);
	if( !RET_IS_OK(r) )
		return r;

	r = references_delete(refs,referee,&filekeys,NULL);
	strlist_done(&filekeys);
	return r;
}

/* Add references for the given source */
retvalue sources_reference(DB *refs,const char *referee,const char *dir,const struct strlist *files) {
	struct strlist filekeys;
	retvalue r;

	r = sources_getfilekeys(dir,files,&filekeys);
	if( !RET_IS_OK(r) )
		return r;

	r = references_insert(refs,referee,&filekeys,NULL);
	strlist_done(&filekeys);
	return r;
}
