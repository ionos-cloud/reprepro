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
extern int force;

/* traverse through a '\n' sepeated lit of "<md5sum> <size> <filename>" 
 * > 0 while entires found, ==0 when not, <0 on error */
retvalue sources_getfile(const char **files,char **filename,char **md5andsize) {
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
	if( filename )
		*filename = filen;
	else
		free(filen);

//	fprintf(stderr,"'%s' -> '%s' \n",*filename,*md5andsize);
	
	return RET_OK;
}

/* get the intresting information out of a "Sources.gz"-chunk */
static retvalue sources_parse_chunk(const char *chunk,char **packagename,char **origdirectory,char **files) {
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

//typedef retvalue source_package_action(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,int hadold);

/* call <data> for each package in the "Sources.gz"-style file <source_file> missing in
 * <pkgs> and using <part> as subdir of pool (i.e. "main","contrib",...) for generated paths */
retvalue sources_add(DB *pkgs,const char *part,const char *sources_file, source_package_action action,void *data) {
	gzFile *fi;
	char *chunk,*oldchunk;
	char *package,*directory,*olddirectory,*files;
	int hadold=0;
	retvalue r,result;

	fi = gzopen(sources_file,"r");
	if( !fi ) {
		fprintf(stderr,"Unable to open file %s\n",sources_file);
		return RET_ERRNO(errno);
	}
	result = RET_NOTHING;
	while( (chunk = chunk_read(fi))) {
		package = NULL; olddirectory = NULL; files = NULL;
		r = sources_parse_chunk(chunk,&package,&olddirectory,&files);
		if( r == RET_NOTHING ) {
err:
			free(chunk);
			free(package);free(files);
			free(olddirectory);
			gzclose(fi);
			return RET_ERROR;
		} else if( RET_WAS_ERROR(r) ) {
			free(chunk);
			free(package);free(files);
			free(olddirectory);
			gzclose(fi);
			return r;
		}else if( RET_IS_OK(r)) {
			hadold = 0;
			oldchunk = packages_get(pkgs,package);
			if( oldchunk && (r=sources_isnewer(chunk,oldchunk)) != 0 ) {
				if( r < 0 ) {
					fprintf(stderr,"Omitting %s because of parse errors.\n",package);
					goto err;
				}
				/* old package may be to be removed */
				hadold=1;
				free(oldchunk);
				oldchunk = NULL;
			}
			if( oldchunk == NULL ) {
				/* add source package */

				directory =  calc_sourcedir(part,package);
				if( !directory )
					goto err;

				r = (action)(data,chunk,package,directory,olddirectory,files,hadold);
				free(directory);

				if( RET_WAS_ERROR(r) && !force)
					goto err;
				RET_UPDATE(result,r);

			} else
				free(oldchunk);
			
			free(package);free(files);
			free(olddirectory);
		}
		free(chunk);
	}
	gzclose(fi);
	return result;
}
