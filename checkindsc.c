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
#include <db.h>
#include "error.h"
#include "strlist.h"
#include "md5sum.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "checkindsc.h"
#include "reference.h"
#include "packages.h"
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

static void dsc_free(struct dscpackage *pkg) {
	if( pkg ) {
		free(pkg->package);free(pkg->version);
		free(pkg->control);
		strlist_done(&pkg->basenames);strlist_done(&pkg->md5sums);
		free(pkg->section);
		free(pkg->priority);
		free(pkg->component);
		free(pkg->dscmd5sum);
		free(pkg->directory);free(pkg->dscbasename);free(pkg->dscfilekey);
		strlist_done(&pkg->filekeys);
	}
	free(pkg);
}

static retvalue dsc_read(struct dscpackage **pkg, const char *filename) {
	retvalue r;
	struct dscpackage *dsc;


	dsc = calloc(1,sizeof(struct dscpackage));

	r = signature_readsignedchunk(filename,&dsc->control);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	if( verbose > 100 ) {
		fprintf(stderr,"Extracted control chunk from '%s': '%s'\n",filename,dsc->control);
	}

	/* first look for fields that should be there */

	r = chunk_getname(dsc->control,"Source",&dsc->package,0);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Source'-header in %s!\n",filename);
		r = RET_ERROR;
	}
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
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}

	r = getvalue_n(dsc->control,"Section",&dsc->section);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	r = getvalue_n(dsc->control,"Priority",&dsc->priority);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	r = sources_parse_getmd5sums(dsc->control,&dsc->basenames,&dsc->md5sums);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	*pkg = dsc;

	return RET_OK;
}

static retvalue dsc_calclocations(struct dscpackage *pkg,const char *filekey,const char *basename,const char *directory) {
	retvalue r;

	assert( pkg != NULL && pkg->package != NULL && pkg->version != NULL );
	assert( pkg->component != NULL );

	if( basename )
		pkg->dscbasename = strdup(basename);
	else
		pkg->dscbasename = calc_source_basename(pkg->package,pkg->version);
	if( pkg->dscbasename == NULL ) {
		return RET_ERROR_OOM;
	}

	if( directory )
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
	if( filekey )
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
	if( RET_WAS_ERROR(r) )
		return r;
	pkg->dscbasename = NULL;

	r = strlist_include(&pkg->md5sums,pkg->dscmd5sum);
	if( RET_WAS_ERROR(r) )
		return r;
	pkg->dscmd5sum = NULL;

	r = strlist_include(&pkg->filekeys,pkg->dscfilekey);
	if( RET_WAS_ERROR(r) )
		return r;
	pkg->dscfilekey = NULL;

	return RET_OK;
}


static retvalue dsc_complete(struct dscpackage *pkg) {
	retvalue r;
	struct fieldtoadd *name;
	struct fieldtoadd *dir;
	char *newchunk,*newchunk2;
	char *newfilelines;

	assert(pkg->section != NULL && pkg->priority != NULL);

	/* first replace the "Source" with a "Package": */
	name = addfield_new("Package",pkg->package,NULL);
	if( !name )
		return RET_ERROR_OOM;
	name = deletefield_new("Source",name);
	if( !name )
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
	dir = addfield_new("Files",newfilelines,NULL);
	if( dir )
		dir = addfield_new("Directory",pkg->directory,dir);
	if( dir )
		dir = deletefield_new("Status",dir);
	if( dir )
		dir = addfield_new("Section",pkg->section,dir);
	if( dir )
		dir = addfield_new("Priority",pkg->priority,dir);
	if( !dir ) {
		free(newfilelines);
		free(newchunk2);
		return RET_ERROR_OOM;
	}
		
	// TODO: add overwriting of other fields here, (before the rest)
	
	newchunk  = chunk_replacefields(newchunk2,dir,"Files");
	free(newfilelines);
	free(newchunk2);
	addfield_free(dir);
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
			struct dscpackage *pkg,const char *dscfilename) {
	char *sourcedir;
	retvalue r;

	r = files_checkin(filesdb,pkg->dscfilekey,dscfilename,&pkg->dscmd5sum);
	if( RET_WAS_ERROR(r) )
		return r;

	r = dirs_getdirectory(dscfilename,&sourcedir);
	if( RET_WAS_ERROR(r) )
		return r;

	r = files_checkinfiles(filesdb,sourcedir,&pkg->basenames,&pkg->filekeys,&pkg->md5sums);

	free(sourcedir);

	return r;
}

/* Check the files needed and set the required fields */
static retvalue dsc_checkfiles(filesdb filesdb,
			struct dscpackage *pkg,const char *dscmd5sum) {
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

retvalue dsc_add(const char *dbdir,DB *references,filesdb filesdb,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *dscfilename,const char *filekey,const char *basename,const char *directory,const char *md5sum,int force){
	retvalue r;
	struct dscpackage *pkg;

	/* First taking a closer look to the file: */

	r = dsc_read(&pkg,dscfilename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	if( forcesection ) {
		free(pkg->section);
		pkg->section = strdup(forcesection);
		if( pkg->section == NULL ) {
			dsc_free(pkg);
			return RET_ERROR_OOM;
		}
	}
	if( forcepriority ) {
		free(pkg->priority);
		pkg->priority = strdup(forcepriority);
		if( pkg->priority == NULL ) {
			dsc_free(pkg);
			return RET_ERROR_OOM;
		}
	}

	/* look for overwrites */

	// TODO: look for overwrites and things like this here...
	// TODO: set pkg->section to new value if doing so.
	
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
		if( filekey && basename && directory && md5sum)
			r = dsc_checkfiles(filesdb,pkg,md5sum);
		else
			r = dsc_copyfiles(filesdb,pkg,dscfilename);
	}

	/* Calculate the chunk to include: */
	
	if( !RET_WAS_ERROR(r) )
		r = dsc_adddsc(pkg);

	if( !RET_WAS_ERROR(r) )
		r = dsc_complete(pkg);

	/* finaly put it into the source distribution */
	if( !RET_WAS_ERROR(r) )
		r = sources_addtodist(dbdir,references,distribution->codename,
				pkg->component,pkg->package,pkg->version,
				pkg->control,&pkg->filekeys);

	dsc_free(pkg);

	return r;
}
