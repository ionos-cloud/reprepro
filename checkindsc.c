/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>
#include <zlib.h>
#include "error.h"
#include "strlist.h"
#include "md5sum.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "checkindsc.h"
#include "reference.h"
#include "signature.h"
#include "sources.h"
#include "files.h"
#include "guesscomponent.h"

extern int verbose;

/* This file includes the code to include sources, i.e.
 to create the chunk for the Sources.gz-file and 
 to put it in the various databases.

things to do with .dsc's checkin by hand: (by comparison with apt-ftparchive)
* Get all from .dsc (search the chunk with
  the Source:-field. end the chunk artifical
  before the pgp-end-block.(in case someone
  missed the newline there))

* check to have source,version,maintainer,
  standards-version, files. And also look
  at binary,architecture and build*, as
  described in policy 5.4

* Get overwrite information, ecspecially
  the priority(if there is a binaries field,
  check the one with the highest) and the section 
  (...what else...?)

* Rename Source-Field to Package-Field

* add dsc to files-list. (check other files md5sum and size)

* add Directory-field

* Add Priority and Status

* apply possible maintainer-updates from the overwrite-file
  or arbitrary tag changes from the extra-overwrite-file

* keep rest (perhaps sort alphabetical)

*/

static inline retvalue getvalue(const char *filename,const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing '%s'-header in %s!\n",field,filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue checkvalue(const char *filename,const char *chunk,const char *field) {
	retvalue r;

	r = chunk_checkfield(chunk,field);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Cannot find '%s'-header in %s!\n",field,filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue getvalue_n(const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		*value = NULL;
	}
	return r;
}

struct dscpackage {
	/* things to be set by dsc_read: */
	char *package,*version;
	char *control;
	struct strlist basenames,md5sums;
	/* things that might still be NULL then: */
	char *section;
	char *priority;
	/* things that will still be NULL then: */
	char *component; //This might be const, too and save some strdups, but...
	/* calculated by dsc_copyfiles or set by dsc_checkfiles */
	char *dscmd5sum;
	/* Things that may be calculated by dsc_calclocations: */
	char *directory, *dscbasename, *dscfilekey;
	struct strlist filekeys;
};

static void dsc_free(/*@only@*/struct dscpackage *pkg) {
	if( pkg != NULL ) {
		free(pkg->package);free(pkg->version);
		free(pkg->control);
		strlist_done(&pkg->basenames);strlist_done(&pkg->md5sums);
		free(pkg->section);
		free(pkg->priority);
		free(pkg->component);
		free(pkg->dscmd5sum);
		free(pkg->directory);free(pkg->dscbasename);free(pkg->dscfilekey);
		strlist_done(&pkg->filekeys);
		free(pkg);
	}
}

static retvalue dsc_read(/*@out@*/struct dscpackage **pkg, const char *filename, bool_t onlysigned) {
	retvalue r;
	struct dscpackage *dsc;


	dsc = calloc(1,sizeof(struct dscpackage));

	r = signature_readsignedchunk(filename,&dsc->control,onlysigned);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	if( verbose > 100 ) {
		fprintf(stderr,"Extracted control chunk from '%s': '%s'\n",filename,dsc->control);
	}

	/* first look for fields that should be there */

	r = chunk_getname(dsc->control,"Source",&dsc->package,FALSE);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Source'-header in %s!\n",filename);
		r = RET_ERROR;
	}
	if( RET_IS_OK(r) )
		r = propersourcename(dsc->package);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	r = checkvalue(filename,dsc->control,"Maintainer");
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	/* only recommended, so ignore errors with this: */
	(void) checkvalue(filename,dsc->control,"Standards-Version");

	r = getvalue(filename,dsc->control,"Version",&dsc->version);
	if( RET_IS_OK(r) ) {
		r = properversion(dsc->version);
	}
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}

	r = getvalue_n(dsc->control,SECTION_FIELDNAME,&dsc->section);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	r = getvalue_n(dsc->control,PRIORITY_FIELDNAME,&dsc->priority);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	r = sources_parse_getmd5sums(dsc->control,&dsc->basenames,&dsc->md5sums);
	if( RET_IS_OK(r) )
		r = properfilenames(&dsc->basenames);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	*pkg = dsc;

	return RET_OK;
}

static retvalue dsc_calclocations(struct dscpackage *pkg,/*@null@*/const char *filekey,/*@null@*/const char *basename,/*@null@*/const char *directory) {
	retvalue r;

	assert( pkg != NULL && pkg->package != NULL && pkg->version != NULL );
	assert( pkg->component != NULL );

	if( basename != NULL )
		pkg->dscbasename = strdup(basename);
	else
		pkg->dscbasename = calc_source_basename(pkg->package,pkg->version);
	if( pkg->dscbasename == NULL ) {
		return RET_ERROR_OOM;
	}

	if( directory != NULL )
		pkg->directory = strdup(directory);
	else
		pkg->directory = calc_sourcedir(pkg->component,pkg->package);
	if( pkg->directory == NULL ) {
		return RET_ERROR_OOM;
	}
	
	/* Calculate the filekeys: */
	r = calc_dirconcats(pkg->directory,&pkg->basenames,&pkg->filekeys);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	if( filekey != NULL )
		pkg->dscfilekey = strdup(filekey);
	else
		pkg->dscfilekey = calc_dirconcat(pkg->directory,pkg->dscbasename);
	if( pkg->dscfilekey == NULL ) {
		return RET_ERROR_OOM;
	}
	return RET_OK;
}

/* Add the dsc-file to basenames,filekeys and md5sums, so that it will
 * be referenced and listed in the Sources.gz */
static retvalue dsc_adddsc(struct dscpackage *pkg) {
	retvalue r;

	r = strlist_include(&pkg->basenames,pkg->dscbasename);
	pkg->dscbasename = NULL;
	if( RET_WAS_ERROR(r) )
		return r;

	r = strlist_include(&pkg->md5sums,pkg->dscmd5sum);
	pkg->dscmd5sum = NULL;
	if( RET_WAS_ERROR(r) )
		return r;

	r = strlist_include(&pkg->filekeys,pkg->dscfilekey);
	pkg->dscfilekey = NULL;
	if( RET_WAS_ERROR(r) )
		return r;

	return RET_OK;
}


static retvalue dsc_complete(struct dscpackage *pkg,const struct overrideinfo *override) {
	retvalue r;
	struct fieldtoadd *name;
	struct fieldtoadd *replace;
	char *newchunk,*newchunk2;
	char *newfilelines;

	assert(pkg->section != NULL && pkg->priority != NULL);

	/* first replace the "Source" with a "Package": */
	name = addfield_new("Package",pkg->package,NULL);
	if( name == NULL )
		return RET_ERROR_OOM;
	name = deletefield_new("Source",name);
	if( name == NULL )
		return RET_ERROR_OOM;
	newchunk2  = chunk_replacefields(pkg->control,name,"Format");
	addfield_free(name);
	if( newchunk2 == NULL ) {
		return RET_ERROR_OOM;
	}

	r = sources_calcfilelines(&pkg->basenames,&pkg->md5sums,&newfilelines);
	if( RET_WAS_ERROR(r) ) {
		free(newchunk2);
		return RET_ERROR_OOM;
	}
	replace = addfield_new("Files",newfilelines,NULL);
	if( replace != NULL )
		replace = addfield_new("Directory",pkg->directory,replace);
	if( replace != NULL )
		replace = deletefield_new("Status",replace);
	if( replace != NULL )
		replace = addfield_new(SECTION_FIELDNAME,pkg->section,replace);
	if( replace != NULL )
		replace = addfield_new(PRIORITY_FIELDNAME,pkg->priority,replace);
	if( replace != NULL )
		replace = override_addreplacefields(override,replace);
	if( replace == NULL ) {
		free(newfilelines);
		free(newchunk2);
		return RET_ERROR_OOM;
	}
	
	newchunk  = chunk_replacefields(newchunk2,replace,"Files");
	free(newfilelines);
	free(newchunk2);
	addfield_free(replace);
	if( newchunk == NULL ) {
		return RET_ERROR_OOM;
	}

	free(pkg->control);
	pkg->control = newchunk;

	return RET_OK;
}

/* Get the files from the directory dscfilename is residing it, and copy
 * them into the pool, also setting pkg->dscmd5sum */
static retvalue dsc_copyfiles(filesdb filesdb,
			struct dscpackage *pkg,const char *dscfilename,int delete) {
	char *sourcedir;
	retvalue r;

	r = files_include(filesdb,dscfilename,pkg->dscfilekey,NULL,&pkg->dscmd5sum,delete);
	if( RET_WAS_ERROR(r) )
		return r;

	r = dirs_getdirectory(dscfilename,&sourcedir);
	if( RET_WAS_ERROR(r) )
		return r;

	r = files_includefiles(filesdb,sourcedir,&pkg->basenames,&pkg->filekeys,&pkg->md5sums,delete);

	free(sourcedir);

	return r;
}

/* Check the files needed and set the required fields */
static retvalue dsc_checkfiles(filesdb filesdb,
			struct dscpackage *pkg,/*@null@*/const char *dscmd5sum) {
	retvalue r;

	/* The code we got should have already put the .dsc in the pool
	 * and calculated its md5sum, so we just use it here: */
	pkg->dscmd5sum = strdup(dscmd5sum);
	if( pkg->dscmd5sum == NULL )
		return RET_ERROR_OOM;

	r = files_expectfiles(filesdb,&pkg->filekeys,&pkg->md5sums);

	return r;
}

/* insert the given .dsc into the mirror in <component> in the <distribution>
 * if component is NULL, guessing it from the section.
 * If basename, filekey and directory are != NULL, then they are used instead 
 * of beeing newly calculated. 
 * (And all files are expected to already be in the pool). */

retvalue dsc_add(const char *dbdir,references refs,filesdb filesdb,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *dscfilename,const char *filekey,const char *basename,const char *directory,const char *md5sum,const struct overrideinfo *srcoverride,int force,int delete,struct strlist *dereferencedfilekeys, bool_t onlysigned){
	retvalue r;
	struct dscpackage *pkg;
	const struct overrideinfo *oinfo;

	//TODO: add some check here to make sure it is really a .dsc file...

	/* First make sure this distribution has a source section at all,
	 * for which it has to be listed in the "Architectures:"-field ;-) */
	if( !strlist_in(&distribution->architectures,"source") ) {
		fprintf(stderr,"Cannot put a source package into Distribution '%s' not having 'source' in its 'Architectures:'-field!\n",distribution->codename);
		// nota bene: this cannot be forced or ignored, as no target has
		// been created for this..
		return RET_ERROR;
	}

	/* Then take a closer look to the file: */

	r = dsc_read(&pkg,dscfilename,onlysigned);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	oinfo = override_search(srcoverride,pkg->package);
	if( forcesection == NULL ) {
		forcesection = override_get(oinfo,SECTION_FIELDNAME);
	}
	if( forcepriority == NULL ) {
		forcepriority = override_get(oinfo,PRIORITY_FIELDNAME);
	}

	if( forcesection != NULL ) {
		free(pkg->section);
		pkg->section = strdup(forcesection);
		if( pkg->section == NULL ) {
			dsc_free(pkg);
			return RET_ERROR_OOM;
		}
	}
	if( forcepriority != NULL ) {
		free(pkg->priority);
		pkg->priority = strdup(forcepriority);
		if( pkg->priority == NULL ) {
			dsc_free(pkg);
			return RET_ERROR_OOM;
		}
	}

	if( pkg->section == NULL ) {
		fprintf(stderr,"No section was given for '%s', skipping.\n",pkg->package);
		dsc_free(pkg);
		return RET_ERROR;
	}
	if( pkg->priority == NULL ) {
		fprintf(stderr,"No priority was given for '%s', skipping.\n",pkg->package);
		dsc_free(pkg);
		return RET_ERROR;
	}
	
	/* decide where it has to go */

	r = guess_component(distribution->codename,&distribution->components,
			pkg->package,pkg->section,forcecomponent,
			&pkg->component);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		return r;
	}
	if( verbose > 0 && forcecomponent == NULL ) {
		fprintf(stderr,"%s: component guessed as '%s'\n",dscfilename,pkg->component);
	}

	r = dsc_calclocations(pkg,filekey,basename,directory);

	/* then looking if we already have this, or copy it in */

	if( !RET_WAS_ERROR(r) ) {
		if( filekey != NULL && basename != NULL && 
				directory != NULL && md5sum != NULL) {
			assert( delete == D_INPLACE );
			r = dsc_checkfiles(filesdb,pkg,md5sum);
		} else
			r = dsc_copyfiles(filesdb,pkg,dscfilename,delete);
	}

	/* Calculate the chunk to include: */
	
	if( !RET_WAS_ERROR(r) )
		r = dsc_adddsc(pkg);

	if( !RET_WAS_ERROR(r) )
		r = dsc_complete(pkg,oinfo);

	/* finaly put it into the source distribution */
	if( !RET_WAS_ERROR(r) ) {
		struct target *t = distribution_getpart(distribution,pkg->component,"source","dsc");
		r = target_initpackagesdb(t,dbdir);
		if( !RET_WAS_ERROR(r) ) {
			retvalue r2;
			r = target_addpackage(t,refs,pkg->package,pkg->version,pkg->control,&pkg->filekeys,force,FALSE,dereferencedfilekeys);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r,r2);
		}
	}
	dsc_free(pkg);

	return r;
}
