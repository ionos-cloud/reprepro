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

// This file shall include the code to include sources, i.e.
// create the chunk of the Sources.gz-file and 
// putting it in the various databases.

// should superseed the add_source from main.c for inclusion
// of downloaded packages from main.c

/* things to do with .dsc's checkin by hand: (by comparison with apt-ftparchive)
Get all from .dsc (search the chunk with
the Source:-field. end the chunk artifical
before the pgp-end-block.(in case someone
missed the newline there))

* check to have source,version,maintainer,
  standards-version, files. And also look
  at binary,architecture and build*, as
  described in policy 5.4

Get overwrite information, ecspecially
the priority(if there is a binaries field,
check the one with the highest) and the section 
(...what else...?)

- Rename Source-Field to Package-Field

- add dsc to files-list. (check other files md5sum and size)

- add Directory-field

- Add Priority and Statues

- apply possible maintainer-updates from the overwrite-file
  or arbitrary tag changes from the extra-overwrite-file

- keep rest (perhaps sort alphabetical)

*/

static inline retvalue getvalue(const char *filename,const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Cannot find %s-header in control file of %s!\n",field,filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue checkvalue(const char *filename,const char *chunk,const char *field) {
	retvalue r;

	r = chunk_checkfield(chunk,field);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Cannot find %s-header in control file of %s!\n",field,filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue getvalue_d(const char *defaul,const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		*value = strdup(defaul);
		if( *value == NULL )
			r = RET_ERROR_OOM;
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

void dsc_free(struct dscpackage *pkg) {
	if( pkg ) {
		free(pkg->package);free(pkg->version);
		free(pkg->control);
		free(pkg->section);free(pkg->priority);free(pkg->component);
		free(pkg->directory);
	}
	free(pkg);
}

retvalue dsc_read(struct dscpackage **pkg, const char *filename) {
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

	r = getvalue(filename,dsc->control,"Source",&dsc->package);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	r = checkvalue(filename,dsc->control,"Maintainer");
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
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
	*pkg = dsc;

	return RET_OK;
}

retvalue dsc_complete(struct dscpackage *pkg) {
	struct fieldtoadd *name;
	struct fieldtoadd *dir;
	char *newchunk,*newchunk2;

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

	dir = addfield_new("Directory",pkg->directory,NULL);
	if( !dir ) {
		free(newchunk2);
		return RET_ERROR_OOM;
	}
	dir = deletefield_new("Status",dir);
	if( !dir ) {
		free(newchunk2);
		return RET_ERROR_OOM;
	}
	dir = addfield_new("Section",pkg->section,dir);
	if( !dir ) {
		free(newchunk2);
		return RET_ERROR_OOM;
	}
	dir = addfield_new("Priority",pkg->priority,dir);
	if( !dir ) {
		free(newchunk2);
		return RET_ERROR_OOM;
	}
		
	// TODO: add overwriting of other fields here, (before the rest)
	
	// TODO: ******** ADD .DSC TO FILES-ITEM ********
	
	newchunk  = chunk_replacefields(newchunk2,dir,"Files");
	free(newchunk2);
	addfield_free(dir);
	if( newchunk == NULL ) {
		return RET_ERROR_OOM;
	}

	free(pkg->control);
	pkg->control = newchunk;

	return RET_OK;
}

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <d->architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if component is NULL, guessing it from the section. */
// TODO: add something to compare files' md5sums to those in the .changes file.
// (Perhaps also importing all those first, such that the database-code handles this)

retvalue dsc_add(const char *dbdir,DB *references,DB *filesdb,const char *mirrordir,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *dscfilename,int force){
	retvalue r,result;
	struct dscpackage *pkg;
	struct strlist filekeys,md5sums,files;
	char *sourcedir;

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

	pkg->directory = calc_sourcedir(pkg->component,pkg->package);
	if( pkg->directory == NULL ) {
		dsc_free(pkg);
		return RET_ERROR_OOM;
	}
	
	/* calculate the needed files: */
	sources_calcfilekeys(pkg->directory,pkg->control,&files,&filekeys,&md5sums);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		return r;
	}

	r = dirs_getdirectory(dscfilename,&sourcedir);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		return r;
	}

	/* then looking if we already have this, or copy it in */

	r = files_checkinfiles(mirrordir,filesdb,sourcedir,&files,&filekeys,&md5sums);
	free(sourcedir);
	strlist_done(&files);
	strlist_done(&md5sums);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		strlist_done(&filekeys);
		return r;
	} 

	r = dsc_complete(pkg);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		strlist_done(&filekeys);
		return r;
	} 
	
	/* finaly put it into the source distribution */

	result = sources_addtodist(dbdir,references,distribution->codename,pkg->component,pkg->package,pkg->version,pkg->control,&filekeys);

	strlist_done(&filekeys);
	dsc_free(pkg);

	return result;
}
