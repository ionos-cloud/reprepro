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
#include "names.h"
#include "chunks.h"
#include "packages.h"
#include "binaries.h"
#include "names.h"
#include "dpkgversions.h"

extern int verbose;

/* get somefields out of a "Packages.gz"-chunk. returns 1 on success, 0 if incomplete, -1 on error */
retvalue binaries_parse_chunk(const char *chunk,char **packagename,char **sourcename,char **basename,struct strlist *md5sums,char **version) {
	char *ppackage;
	retvalue r;
#define IFREE(p) if(p) free(*p);
#define ISFREE(p) if(p) strlist_done(p);

	r  = chunk_getvalue(chunk,"Package", &ppackage);
	if( !RET_IS_OK(r) )
		return r;
	if( !ppackage ) {
		return RET_ERROR_OOM;
	}
	if( packagename ) {
		*packagename = ppackage;
	}

	/* collect the given md5sum and size */

	if( md5sums ) {
		char *pmd5,*psize,*md5andsize;

		r = chunk_getvalue(chunk,"MD5sum",&pmd5);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			return r;
		}
		r = chunk_getvalue(chunk,"Size",&psize);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			free(pmd5);
			return r;
		}
		md5andsize = calc_concatmd5andsize(pmd5,psize);
		free(pmd5);free(psize);
		if( !md5andsize ) {
			free(ppackage);
			return RET_ERROR_OOM;
		}
		r = strlist_init_singleton(md5andsize,md5sums);
		if( RET_WAS_ERROR(r) ) {
			free(md5andsize);
			free(ppackage);
			return r;
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
			ISFREE(md5sums);
			return r;
		}
	}

	/* get the version */
	if( version ) {
		r = chunk_getvalue(chunk,"Version",version);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			IFREE(sourcename);
			return r;
		}
	}

	/* generate a base filename based on package,version and architecture */

	if( basename ) {
		char *parch,*pversion;

		// TODO combine the two looks for version...
		r = chunk_getvalue(chunk,"Version",&pversion);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			ISFREE(md5sums);
			IFREE(sourcename);
			return r;
		}
		r = chunk_getvalue(chunk,"Architecture",&parch);
		if( !RET_IS_OK(r) ) {
			free(ppackage);
			ISFREE(md5sums);
			IFREE(sourcename);
			free(pversion);
			return r;
		}
		/* TODO check parts to consist out of save charakters */
		*basename = calc_package_basename(ppackage,pversion,parch);
		free(pversion);free(parch);
		if( !*basename ) {
			free(ppackage);
			ISFREE(md5sums);
			IFREE(sourcename);
			return RET_ERROR_OOM;
		}
	}

	if( packagename == NULL)
		free(ppackage);

	return RET_OK;
}

/* get files out of a "Packages.gz"-chunk. */
retvalue binaries_parse_getfiles(const char *chunk,struct strlist *files) {
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


/* Look for an old version of the Package in the database,
 * returns RET_NOTHING, if there is none */
retvalue binaries_lookforold( DB *pkgs,const char *name, struct strlist *files) {
	char *oldchunk;
	retvalue r;

	r = packages_get(pkgs,name,&oldchunk);
	if( !RET_IS_OK(r) ) {
		return r;
	}
	r = binaries_parse_getfiles(oldchunk,files);
	free(oldchunk);

	return r;
}

/* Look for an older version of the Package in the database.
 * return RET_NOTHING if there is none, otherwise
 * Set *oldversion, if there is already a newer (or equal) version to
 * <version>  */
retvalue binaries_lookforolder(
		DB *packages,const char *packagename,
		const char *newversion,char **oldversion,
		struct strlist *oldfilekeys) {
	char *oldchunk,*ov;
	retvalue r;

	assert( packages != NULL && packagename != NULL
			&& newversion != NULL && oldversion != NULL
			&& oldfilekeys != NULL );

	r = packages_get(packages,packagename,&oldchunk);
	if( !RET_IS_OK(r) ) {
		return r;
	}

	r = binaries_parse_chunk(oldchunk,NULL,NULL,NULL,NULL,&ov);
	if( !RET_IS_OK(r) ) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"Does not look like binary control: '%s'\n",oldchunk);
			r = RET_ERROR;

		}
		free(oldchunk);
		return r;
	}
	r = dpkgversions_isNewer(newversion,ov);

	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Parse errors processing versions of %s.\n",packagename);
		free(ov);
		free(oldchunk);
		return r;
	}
	if( RET_IS_OK(r) ) {
		*oldversion = NULL;
		free(ov);
	} else
		*oldversion = ov;

	r = binaries_parse_getfiles(oldchunk,oldfilekeys);
	free(oldchunk);
	if( !RET_IS_OK(r) && oldversion )
		free(*oldversion);

	return r;
}

static inline retvalue callaction(new_package_action *action,void *data,
		const char *chunk,const char *packagename,const char *version,
		const char *sourcename,const char *basename, 
		const char *component,
		const struct strlist *md5sums,
		const struct strlist *origfiles,
		const struct strlist *oldfiles) {
	retvalue r;
	char *filekey,*newchunk;
	struct strlist filekeys;

	filekey =  calc_filekey(component,sourcename,basename);
	if( !filekey )
		return RET_ERROR_OOM;
	r = strlist_init_singleton(filekey,&filekeys);
	if( RET_WAS_ERROR(r) ) {
		free(filekey);
		return r;
	}
	// Calculating the following here will cause work done
	// unnecesarrily, but it unifies handling afterwards:
	newchunk = chunk_replacefield(chunk,"Filename",filekey);
	if( !newchunk ) {
		strlist_done(&filekeys);
		return RET_ERROR_OOM;
	}
	r = (*action)(data,newchunk,packagename,version,
			&filekeys,origfiles,md5sums,oldfiles);
	free(newchunk);
	strlist_done(&filekeys);
	return r;
}

struct binaries_add {DB *pkgs; void *data; const char *component; new_package_action *action; };

static retvalue addbinary(void *data,const char *chunk) {
	struct binaries_add *d = data;
	retvalue r;
	char *oldversion;
	struct strlist origfiles,oldfilekeys,md5sums;
	char *package,*basename,*sourcename,*version;

	r = binaries_parse_chunk(chunk,&package,&sourcename,&basename,&md5sums,&version);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Cannot parse chunk: '%s'!\n",chunk);
		return r;
	} else if( r == RET_NOTHING ) {
		fprintf(stderr,"Does not look like a binary package: '%s'!\n",chunk);
		return RET_ERROR;
	}
	assert(RET_IS_OK(r));

	r = binaries_parse_getfiles(chunk,&origfiles);

	if( RET_IS_OK(r) )
		r = binaries_lookforolder(d->pkgs,package,version,
				&oldversion,&oldfilekeys);

	if( RET_IS_OK(r) ) {
		if( oldversion != NULL ) {
			if( verbose > 40 )
				fprintf(stderr,
"Ignoring '%s' with version '%s', as '%s' is already there.\n",
					package,version,oldversion);
			free(oldversion);
			r = RET_NOTHING;
		} else 
			r = callaction(d->action,d->data,chunk,package,version,
				sourcename,basename,d->component,&md5sums,
				&origfiles,&oldfilekeys);
		strlist_done(&oldfilekeys);
	} else if( r == RET_NOTHING ) {
		r = callaction(d->action,d->data,chunk,package,version,
				sourcename,basename,d->component,
				&md5sums,&origfiles,NULL);
	}
	
	strlist_done(&origfiles);
	free(version); free(package);strlist_done(&md5sums);
	free(basename);free(sourcename);
	return r;
}



/* call action for each package in packages_file */
retvalue binaries_findnew(DB *pkgs,const char *component,const char *packages_file, new_package_action action,void *data,int force) {
	struct binaries_add mydata;

	mydata.data=data;
	mydata.pkgs=pkgs;
	mydata.component=component;
	mydata.action=action;

	return chunk_foreach(packages_file,addbinary,&mydata,force,0);
}
