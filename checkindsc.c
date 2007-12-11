/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007 Bernhard R. Link
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
#include "error.h"
#include "strlist.h"
#include "md5sum.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "checkindsc.h"
#include "reference.h"
#include "sources.h"
#include "files.h"
#include "guesscomponent.h"
#include "tracking.h"
#include "ignore.h"
#include "override.h"
#include "log.h"

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

struct dscpackage {
	/* things to be set by dsc_read: */
	struct dsc_headers dsc;
	/* things that will still be NULL then: */
	char *component; //This might be const, too and save some strdups, but...
	/* Things that may be calculated by dsc_calclocations: */
	struct strlist filekeys;
};

void dsc_free(/*@only@*/struct dscpackage *pkg) {
	if( pkg != NULL ) {
		sources_done(&pkg->dsc);
		free(pkg->component);
		strlist_done(&pkg->filekeys);
		free(pkg);
	}
}

static retvalue dsc_read(/*@out@*/struct dscpackage **pkg, const char *filename) {
	retvalue r;
	struct dscpackage *dsc;
	bool broken;


	dsc = calloc(1,sizeof(struct dscpackage));

	r = sources_readdsc(&dsc->dsc, filename, &broken);
	if( RET_IS_OK(r) && broken && !IGNORING_(brokensignatures,
"'%s' contains only broken signatures.\n"
"This most likely means the file was damaged (or edited improperly)\n",
				filename) )
		r = RET_ERROR;
	if( RET_IS_OK(r) )
		r = propersourcename(dsc->dsc.name);
	if( RET_IS_OK(r) )
		r = properversion(dsc->dsc.version);
	if( RET_IS_OK(r) )
		r = properfilenames(&dsc->dsc.basenames);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc);
		return r;
	}
	*pkg = dsc;

	return RET_OK;
}

/* Add the dsc-file to basenames,filekeys and md5sums, so that it will
 * be referenced and listed in the Sources.gz */
static retvalue dsc_adddsc(struct dscpackage *pkg, /*@only@*/char *dscbasename, /*@only@*/char *dscmd5sum, /*@only@*/ char *dscfilekey) {
	retvalue r;

	r = strlist_include(&pkg->dsc.basenames, dscbasename);
	if( RET_WAS_ERROR(r) ) {
		free(dscmd5sum);
		free(dscfilekey);
		return r;
	}

	r = strlist_include(&pkg->dsc.md5sums, dscmd5sum);
	if( RET_WAS_ERROR(r) ) {
		free(dscfilekey);
		return r;
	}

	r = strlist_include(&pkg->filekeys, dscfilekey);
	if( RET_WAS_ERROR(r) )
		return r;

	return RET_OK;
}


retvalue dsc_prepare(struct dscpackage **dsc,struct database *database,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *sourcedir, const char *dscfilename,const char *filekey,const char *basename,const char *directory,const char *md5sum,int delete, const char *expectedname, const char *expectedversion){
	retvalue r;
	struct dscpackage *pkg;
	const struct overrideinfo *oinfo;
	char *dscbasename, *dscmd5sum;
	char *control;
	int i;

	assert( forcesection != NULL );
	assert( forcepriority != NULL );
	assert( forcecomponent != NULL );
	assert( basename != NULL );
	assert( md5sum != NULL );
	assert( expectedname != NULL );
	assert( expectedversion != NULL );

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
	if( strcmp(expectedname, pkg->dsc.name) != 0 ) {
		/* This cannot be ignored, as too much depends on it yet */
		fprintf(stderr,
"'%s' says it is '%s', while .changes file said it is '%s'\n",
				basename, pkg->dsc.name, expectedname);
		dsc_free(pkg);
		return RET_ERROR;
	}
	if( strcmp(expectedversion, pkg->dsc.version) != 0 &&
	    !IGNORING_(wrongversion,
"'%s' says it is version '%s', while .changes file said it is '%s'\n",
				basename, pkg->dsc.version, expectedversion)) {
		dsc_free(pkg);
		return RET_ERROR;
	}

	oinfo = override_search(distribution->overrides.dsc,pkg->dsc.name);

	free(pkg->dsc.section);
	pkg->dsc.section = strdup(forcesection);
	if( pkg->dsc.section == NULL ) {
		dsc_free(pkg);
		return RET_ERROR_OOM;
	}
	free(pkg->dsc.priority);
	pkg->dsc.priority = strdup(forcepriority);
	if( pkg->dsc.priority == NULL ) {
		dsc_free(pkg);
		return RET_ERROR_OOM;
	}

	pkg->component = strdup(forcecomponent);
	if( pkg->component == NULL ) {
		dsc_free(pkg);
		return RET_ERROR_OOM;
	}

	assert( pkg != NULL && pkg->dsc.name != NULL && pkg->dsc.version != NULL );
	assert( pkg->component != NULL );

	/* Add the dsc file to the list of files in this source package: */
	dscbasename = strdup(basename);
	if( dscbasename == NULL )
		r = RET_ERROR_OOM;
	else
		r = strlist_include(&pkg->dsc.basenames, dscbasename);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		return r;
	}

	dscmd5sum = strdup(md5sum);
	if( dscmd5sum == NULL )
		r = RET_ERROR_OOM;
	else
		r = strlist_include(&pkg->dsc.md5sums, dscmd5sum);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		return r;
	}

	/* Calculate the filekeys: */
	r = calc_dirconcats(directory, &pkg->dsc.basenames, &pkg->filekeys);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		return r;
	}

	/* noone else might have looked yet, if we have them: */

	assert( pkg->dsc.basenames.count == pkg->dsc.md5sums.count );
	assert( pkg->dsc.basenames.count == pkg->filekeys.count );
	for( i = 1 ; i < pkg->dsc.basenames.count ; i ++ ) {
		if( !RET_WAS_ERROR(r) ) {
			const char *basename = pkg->dsc.basenames.values[i];
			const char *filekey = pkg->filekeys.values[i];
			const char *md5sum = pkg->dsc.md5sums.values[i];

			r = files_includefile(database, sourcedir,
					basename, filekey, md5sum,
					NULL, delete);
		}
	}

	if( !RET_WAS_ERROR(r) )
		r = sources_complete(&pkg->dsc, directory, oinfo,
				pkg->dsc.section, pkg->dsc.priority, &control);
	if( RET_IS_OK(r) ) {
		free(pkg->dsc.control);
		pkg->dsc.control = control;
		*dsc = pkg;
	} else
		dsc_free(pkg);

	return r;
}

retvalue dsc_addprepared(const struct dscpackage *pkg,struct database *database,struct distribution *distribution,struct strlist *dereferencedfilekeys, struct trackingdata *trackingdata){
	retvalue r;
	struct target *t = distribution_getpart(distribution,pkg->component,"source","dsc");

	assert( logger_isprepared(distribution->logger) );

	/* finally put it into the source distribution */
	r = target_initpackagesdb(t, database, READWRITE);
	if( !RET_WAS_ERROR(r) ) {
		retvalue r2;
		if( interrupted() )
			r = RET_ERROR_INTERRUPTED;
		else
			r = target_addpackage(t, distribution->logger, database,
					pkg->dsc.name, pkg->dsc.version,
					pkg->dsc.control, &pkg->filekeys,
					false, dereferencedfilekeys,
					trackingdata, ft_SOURCE);
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
retvalue dsc_add(struct database *database,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *dscfilename,int delete,struct strlist *dereferencedfilekeys, trackingdb tracks){
	retvalue r;
	struct dscpackage *pkg;
	struct trackingdata trackingdata;
	char *destdirectory, *origdirectory;
	const struct overrideinfo *oinfo;
	char *control;
	int i;



	/* First make sure this distribution has a source section at all,
	 * for which it has to be listed in the "Architectures:"-field ;-) */
	if( !strlist_in(&distribution->architectures,"source") ) {
		fprintf(stderr,"Cannot put a source package into Distribution '%s' not having 'source' in its 'Architectures:'-field!\n",distribution->codename);
		/* nota bene: this cannot be forced or ignored, as no target has
		   been created for this. */
		return RET_ERROR;
	}

	r = dsc_read(&pkg, dscfilename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	oinfo = override_search(distribution->overrides.dsc,pkg->dsc.name);
	if( forcesection == NULL ) {
		forcesection = override_get(oinfo,SECTION_FIELDNAME);
	}
	if( forcepriority == NULL ) {
		forcepriority = override_get(oinfo,PRIORITY_FIELDNAME);
	}

	if( forcesection != NULL ) {
		free(pkg->dsc.section);
		pkg->dsc.section = strdup(forcesection);
		if( pkg->dsc.section == NULL ) {
			dsc_free(pkg);
			return RET_ERROR_OOM;
		}
	}
	if( forcepriority != NULL ) {
		free(pkg->dsc.priority);
		pkg->dsc.priority = strdup(forcepriority);
		if( pkg->dsc.priority == NULL ) {
			dsc_free(pkg);
			return RET_ERROR_OOM;
		}
	}

	if( pkg->dsc.section == NULL ) {
		fprintf(stderr, "No section was given for '%s', skipping.\n",pkg->dsc.name);
		dsc_free(pkg);
		return RET_ERROR;
	}
	if( pkg->dsc.priority == NULL ) {
		fprintf(stderr, "No priority was given for '%s', skipping.\n",pkg->dsc.name);
		dsc_free(pkg);
		return RET_ERROR;
	}

	/* decide where it has to go */

	r = guess_component(distribution->codename, &distribution->components,
			pkg->dsc.name, pkg->dsc.section, forcecomponent,
			&pkg->component);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		return r;
	}
	if( verbose > 0 && forcecomponent == NULL ) {
		fprintf(stderr,"%s: component guessed as '%s'\n",dscfilename,pkg->component);
	}

	r = dirs_getdirectory(dscfilename, &origdirectory);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg);
		return r;
	}

	{	char *dscbasename, *dscmd5sum, *dscfilekey;

		dscbasename = calc_source_basename(pkg->dsc.name, pkg->dsc.version);
		destdirectory = calc_sourcedir(pkg->component, pkg->dsc.name);
		/* Calculate the filekeys: */
		if( destdirectory != NULL )
			r = calc_dirconcats(destdirectory, &pkg->dsc.basenames,
					&pkg->filekeys);
		if( dscbasename == NULL || destdirectory == NULL || RET_WAS_ERROR(r) ) {
			free(dscbasename);
			free(destdirectory); free(origdirectory);
			dsc_free(pkg);
			return r;
		}
		dscfilekey = calc_dirconcat(destdirectory, dscbasename);
		dscmd5sum = NULL;
		if( dscfilename == NULL )
			r = RET_ERROR_OOM;
		else
			/* then look if we already have this, or copy it in */
			r = files_include(database,
					dscfilename, dscfilekey,
					NULL, &dscmd5sum, delete);

		if( !RET_WAS_ERROR(r) )
			r = dsc_adddsc(pkg, dscbasename, dscmd5sum, dscfilekey);
		else {
			free(dscmd5sum);
			free(dscfilekey);
			free(dscbasename);
		}
	}

	assert( pkg->dsc.basenames.count == pkg->dsc.md5sums.count );
	assert( pkg->dsc.basenames.count == pkg->filekeys.count );
	for( i = 1 ; i < pkg->dsc.basenames.count ; i ++ ) {
		if( !RET_WAS_ERROR(r) ) {
			const char *basename = pkg->dsc.basenames.values[i];
			const char *filekey = pkg->filekeys.values[i];
			const char *md5sum = pkg->dsc.md5sums.values[i];

			r = files_includefile(database, origdirectory,
					basename, filekey, md5sum,
					NULL, delete);
		}
	}
	free(origdirectory);

	/* Calculate the chunk to include: */

	if( !RET_WAS_ERROR(r) )
		r = sources_complete(&pkg->dsc, destdirectory, oinfo,
				pkg->dsc.section, pkg->dsc.priority, &control);
	free(destdirectory);
	if( RET_IS_OK(r) ) {
		free(pkg->dsc.control);
		pkg->dsc.control = control;
	} else {
		dsc_free(pkg);
		return r;
	}

	if( interrupted() ) {
		dsc_free(pkg);
		return RET_ERROR_INTERRUPTED;
	}

	if( tracks != NULL ) {
		r = trackingdata_summon(tracks,pkg->dsc.name,pkg->dsc.version,&trackingdata);
		if( RET_WAS_ERROR(r) ) {
			dsc_free(pkg);
			return r;
		}
	}

	r = dsc_addprepared(pkg, database, distribution,
			dereferencedfilekeys,
			(tracks!=NULL)?&trackingdata:NULL);
	dsc_free(pkg);

	if( tracks != NULL ) {
		retvalue r2;
		r2 = trackingdata_finish(tracks, &trackingdata, database,
				dereferencedfilekeys);
		RET_ENDUPDATE(r,r2);
	}
	return r;
}
