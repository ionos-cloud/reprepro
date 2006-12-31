/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
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
#include "tracking.h"
#include "ignore.h"

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
	/* calculated or set by files_include of the dsc */
	char *dscmd5sum;
	/* Things that may be calculated by dsc_calclocations: */
	char *directory, *dscbasename, *dscfilekey;
	struct strlist filekeys;
};

void dsc_free(/*@only@*/struct dscpackage *pkg) {
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

static retvalue dsc_read(/*@out@*/struct dscpackage **pkg, const char *filename) {
	retvalue r;
	struct dscpackage *dsc;
	bool_t broken;


	dsc = calloc(1,sizeof(struct dscpackage));

	r = signature_readsignedchunk(filename,&dsc->control,NULL,NULL, &broken);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	if( broken && !IGNORING_(brokensignatures,
"'%s' contains only broken signatures.\n"
"This most likely means the file was damaged (or edited improperly)\n",
				filename) ) {
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

retvalue dsc_prepare(struct dscpackage **dsc,filesdb filesdb,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *sourcedir, const char *dscfilename,const char *filekey,const char *basename,const char *directory,const char *md5sum,int delete, const char *expectedname, const char *expectedversion){
	retvalue r;
	struct dscpackage *pkg;
	const struct overrideinfo *oinfo;

	/* First make sure this distribution has a source section at all,
	 * for which it has to be listed in the "Architectures:"-field ;-) */
	if( !strlist_in(&distribution->architectures,"source") ) {
		fprintf(stderr,"Cannot put a source package into Distribution '%s' not having 'source' in its 'Architectures:'-field!\n",distribution->codename);
		/* nota bene: this cannot be forced or ignored, as no target has
		   been created for this. */
		return RET_ERROR;
	}

	/* Then take a closer look in the file: */

	r = dsc_read(&pkg,dscfilename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	if( expectedname != NULL &&
	    strcmp(expectedname, pkg->package) != 0 ) {
		/* This cannot be ignored, as too much depends on it yet */
		fprintf(stderr,
"'%s' says it is '%s', while .changes file said it is '%s'\n",
				basename, pkg->package, expectedname);
		dsc_free(pkg);
		return RET_ERROR;
	}
	if( expectedversion != NULL &&
	    strcmp(expectedversion, pkg->version) != 0 &&
	    !IGNORING_(wrongversion,
"'%s' says it is version '%s', while .changes file said it is '%s'\n",
				basename, pkg->version, expectedversion)) {
		dsc_free(pkg);
		return RET_ERROR;
	}

	oinfo = override_search(distribution->overrides.dsc,pkg->package);
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

	/* then look if we already have this, or copy it in */

	if( !RET_WAS_ERROR(r) ) {
		/* evil things will hapen if delete is not D_INPLACE when
		 * called from includechanges */
		r = files_include(filesdb,dscfilename,pkg->dscfilekey,md5sum,&pkg->dscmd5sum,delete);
	}

	if( !RET_WAS_ERROR(r) ) {
		r = files_includefiles(filesdb,sourcedir,&pkg->basenames,&pkg->filekeys,&pkg->md5sums,delete);
	}

	/* Calculate the chunk to include: */

	if( !RET_WAS_ERROR(r) )
		r = dsc_adddsc(pkg);

	if( !RET_WAS_ERROR(r) )
		r = dsc_complete(pkg,oinfo);

	if( RET_IS_OK(r) )
		*dsc = pkg;
	else
		dsc_free(pkg);

	return r;
}

retvalue dsc_addprepared(const struct dscpackage *pkg,const char *dbdir,references refs,struct distribution *distribution,struct strlist *dereferencedfilekeys, struct trackingdata *trackingdata){
	retvalue r;
	struct target *t = distribution_getpart(distribution,pkg->component,"source","dsc");

	/* finally put it into the source distribution */
	r = target_initpackagesdb(t,dbdir);
	if( !RET_WAS_ERROR(r) ) {
		retvalue r2;
		if( interrupted() )
			r = RET_ERROR_INTERUPTED;
		else
			r = target_addpackage(t,refs,pkg->package,pkg->version,pkg->control,&pkg->filekeys,FALSE,dereferencedfilekeys,trackingdata,ft_SOURCE);
		r2 = target_closepackagesdb(t);
		RET_ENDUPDATE(r,r2);
	}
	RET_UPDATE(distribution->status, r);
	return r;
}

/* insert the given .dsc into the mirror in <component> in the <distribution>
 * if component is NULL, guessing it from the section.
 * If basename, filekey and directory are != NULL, then they are used instead
 * of being newly calculated.
 * (And all files are expected to already be in the pool). */
retvalue dsc_add(const char *dbdir,references refs,filesdb filesdb,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *dscfilename,int delete,struct strlist *dereferencedfilekeys, trackingdb tracks){
	retvalue r;
	struct dscpackage *pkg;
	struct trackingdata trackingdata;
	char *dscdirectory;

	r = dirs_getdirectory(dscfilename,&dscdirectory);
	if( RET_WAS_ERROR(r) )
		return r;

	r = dsc_prepare(&pkg,filesdb,forcecomponent,forcesection,forcepriority,distribution,dscdirectory,dscfilename,NULL,NULL,NULL,NULL,delete,NULL,NULL);
	free(dscdirectory);
	if( RET_WAS_ERROR(r) )
		return r;

	if( interrupted() ) {
		dsc_free(pkg);
		return RET_ERROR_INTERUPTED;
	}

	if( tracks != NULL ) {
		r = trackingdata_summon(tracks,pkg->package,pkg->version,&trackingdata);
		if( RET_WAS_ERROR(r) ) {
			dsc_free(pkg);
			return r;
		}
	}

	r = dsc_addprepared(pkg,dbdir,refs,distribution,
			dereferencedfilekeys,
			(tracks!=NULL)?&trackingdata:NULL);
	dsc_free(pkg);

	if( tracks != NULL ) {
		retvalue r2;
		r2 = trackingdata_finish(tracks, &trackingdata, refs, dereferencedfilekeys);
		RET_ENDUPDATE(r,r2);
	}
	return r;
}
