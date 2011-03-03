/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2008 Bernhard R. Link
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
#include "filecntl.h"
#include "strlist.h"
#include "checksums.h"
#include "names.h"
#include "checksums.h"
#include "dirs.h"
#include "checkindsc.h"
#include "reference.h"
#include "sources.h"
#include "files.h"
#include "guesscomponent.h"
#include "tracking.h"
#include "ignore.h"
#include "override.h"
#include "log.h"
#include "sourceextraction.h"

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
	/* if set, everything set to true should be deleted when not used */
	/*@null@*/bool *deleteonfailure;
};

static void dsc_free(/*@only@*/struct dscpackage *pkg, struct database *database) {
	if( pkg != NULL ) {
		if( pkg->deleteonfailure != NULL ) {
			int i;

			for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
				if( pkg->deleteonfailure[i] )
					files_deleteandremove(
						database,
						pkg->filekeys.values[i],
						true, false);
			}
			free(pkg->deleteonfailure);
		}
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

	r = sources_readdsc(&dsc->dsc, filename, filename, &broken);
	if( RET_IS_OK(r) && broken && !IGNORING_(brokensignatures,
"'%s' contains only broken signatures.\n"
"This most likely means the file was damaged or edited improperly\n",
				filename) )
		r = RET_ERROR;
	if( RET_IS_OK(r) )
		r = propersourcename(dsc->dsc.name);
	if( RET_IS_OK(r) )
		r = properversion(dsc->dsc.version);
	if( RET_IS_OK(r) )
		r = properfilenames(&dsc->dsc.files.names);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(dsc, NULL);
		return r;
	}
	*pkg = dsc;

	return RET_OK;
}

retvalue dsc_addprepared(struct database *database, const struct dsc_headers *dsc, const char *component, const struct strlist *filekeys, bool *usedmarker, struct distribution *distribution, struct strlist *dereferencedfilekeys, struct trackingdata *trackingdata){
	retvalue r;
	struct target *t = distribution_getpart(distribution,component,"source","dsc");

	assert( logger_isprepared(distribution->logger) );

	/* finally put it into the source distribution */
	r = target_initpackagesdb(t, database, READWRITE);
	if( !RET_WAS_ERROR(r) ) {
		retvalue r2;
		if( interrupted() )
			r = RET_ERROR_INTERRUPTED;
		else
			r = target_addpackage(t, distribution->logger, database,
					dsc->name, dsc->version,
					dsc->control, filekeys,
					usedmarker,
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
	bool usedmarker = false;
	int i;

	causingfile = dscfilename;

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
			dsc_free(pkg, database);
			return RET_ERROR_OOM;
		}
	}
	if( forcepriority != NULL ) {
		free(pkg->dsc.priority);
		pkg->dsc.priority = strdup(forcepriority);
		if( pkg->dsc.priority == NULL ) {
			dsc_free(pkg, database);
			return RET_ERROR_OOM;
		}
	}

	r = dirs_getdirectory(dscfilename, &origdirectory);
	if( RET_WAS_ERROR(r) ) {
		dsc_free(pkg, database);
		return r;
	}

	if( pkg->dsc.section == NULL || pkg->dsc.priority == NULL ) {
		struct sourceextraction *extraction;

		extraction = sourceextraction_init(
			(pkg->dsc.section == NULL)?&pkg->dsc.section:NULL,
			(pkg->dsc.priority == NULL)?&pkg->dsc.priority:NULL);
		if( FAILEDTOALLOC(extraction) ) {
			free(origdirectory);
			dsc_free(pkg, database);
			return RET_ERROR_OOM;
		}
		for( i = 0 ; i < pkg->dsc.files.names.count ; i ++ )
			sourceextraction_setpart(extraction, i,
					pkg->dsc.files.names.values[i]);
		while( sourceextraction_needs(extraction, &i) ) {
			char *fullfilename = calc_dirconcat(origdirectory,
					pkg->dsc.files.names.values[i]);
			if( FAILEDTOALLOC(fullfilename) ) {
				free(origdirectory);
				dsc_free(pkg, database);
				return RET_ERROR_OOM;
			}
			/* while it would nice to try at the pool if we
			 * do not have the file here, to know its location
			 * in the pool we need to know the component. And
			 * for the component we might need the section first */
			// TODO: but if forcecomponent is set it might be possible.
			r = sourceextraction_analyse(extraction, fullfilename);
			free(fullfilename);
			if( RET_WAS_ERROR(r) ) {
				free(origdirectory);
				dsc_free(pkg, database);
				sourceextraction_abort(extraction);
				return r;
			}
		}
		r = sourceextraction_finish(extraction);
		if( RET_WAS_ERROR(r) ) {
			free(origdirectory);
			dsc_free(pkg, database);
			return r;
		}
	}

	if( pkg->dsc.section == NULL && pkg->dsc.priority == NULL ) {
		fprintf(stderr, "No section and no priority for '%s', skipping.\n",
				pkg->dsc.name);
		free(origdirectory);
		dsc_free(pkg, database);
		return RET_ERROR;
	}
	if( pkg->dsc.section == NULL ) {
		fprintf(stderr, "No section for '%s', skipping.\n", pkg->dsc.name);
		free(origdirectory);
		dsc_free(pkg, database);
		return RET_ERROR;
	}
	if( pkg->dsc.priority == NULL ) {
		fprintf(stderr, "No priority for '%s', skipping.\n", pkg->dsc.name);
		free(origdirectory);
		dsc_free(pkg, database);
		return RET_ERROR;
	}

	/* decide where it has to go */

	r = guess_component(distribution->codename, &distribution->components,
			pkg->dsc.name, pkg->dsc.section, forcecomponent,
			&pkg->component);
	if( RET_WAS_ERROR(r) ) {
		free(origdirectory);
		dsc_free(pkg, database);
		return r;
	}
	if( verbose > 0 && forcecomponent == NULL ) {
		fprintf(stderr,"%s: component guessed as '%s'\n",dscfilename,pkg->component);
	}

	pkg->deleteonfailure = calloc(pkg->dsc.files.names.count+1, sizeof(bool));
	if( pkg->deleteonfailure == NULL ) {
		dsc_free(pkg, database);
		return RET_ERROR_OOM;
	}

	{	char *dscbasename, *dscfilekey;
		struct checksums *dscchecksums;

		dscbasename = calc_source_basename(pkg->dsc.name, pkg->dsc.version);
		destdirectory = calc_sourcedir(pkg->component, pkg->dsc.name);
		/* Calculate the filekeys: */
		if( destdirectory != NULL )
			r = calc_dirconcats(destdirectory,
					&pkg->dsc.files.names,
					&pkg->filekeys);
		if( dscbasename == NULL || destdirectory == NULL || RET_WAS_ERROR(r) ) {
			free(dscbasename);
			free(destdirectory); free(origdirectory);
			dsc_free(pkg, database);
			return r;
		}
		dscfilekey = calc_dirconcat(destdirectory, dscbasename);
		dscchecksums = NULL;
		if( dscfilename == NULL )
			r = RET_ERROR_OOM;
		else
			/* then look if we already have this, or copy it in */
			r = files_preinclude(database,
					dscfilename, dscfilekey,
					&dscchecksums, &pkg->deleteonfailure[0]);

		if( !RET_WAS_ERROR(r) ) {
			/* Add the dsc-file to basenames,filekeys and md5sums,
			 * so that it will be listed in the Sources.gz */

			r = checksumsarray_include(&pkg->dsc.files,
					dscbasename, dscchecksums);
			if( RET_IS_OK(r) )
				r = strlist_include(&pkg->filekeys, dscfilekey);
			else
				free(dscfilekey);
		} else {
			free(dscfilekey);
			free(dscbasename);
		}
		checksums_free(dscchecksums);
	}

	assert( pkg->dsc.files.names.count == pkg->filekeys.count );
	for( i = 1 ; i < pkg->dsc.files.names.count ; i ++ ) {
		if( !RET_WAS_ERROR(r) ) {
			r = files_checkincludefile(database, origdirectory,
					pkg->dsc.files.names.values[i],
					pkg->filekeys.values[i],
					&pkg->dsc.files.checksums[i],
					&pkg->deleteonfailure[i]);
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
		dsc_free(pkg, database);
		return r;
	}

	if( interrupted() ) {
		dsc_free(pkg, database);
		return RET_ERROR_INTERRUPTED;
	}

	if( tracks != NULL ) {
		r = trackingdata_summon(tracks,pkg->dsc.name,pkg->dsc.version,&trackingdata);
		if( RET_WAS_ERROR(r) ) {
			dsc_free(pkg, database);
			return r;
		}
	}

	r = dsc_addprepared(database, &pkg->dsc, pkg->component,
			&pkg->filekeys, &usedmarker,
			distribution, dereferencedfilekeys,
			(tracks!=NULL)?&trackingdata:NULL);
	if( usedmarker ) {
		for( i = 0 ; i < pkg->dsc.files.names.count ; i++ )
			pkg->deleteonfailure[i] = false;
	}

	/* delete source files, if they are to be */
	if( ( RET_IS_OK(r) && delete >= D_MOVE ) ||
			( r == RET_NOTHING && delete >= D_DELETE ) ) {
		char *origdirectory, *fullfilename;
		retvalue r2;

		r2 = dirs_getdirectory(dscfilename, &origdirectory);
		if( RET_IS_OK(r2) ) {
			for( i = 0 ; i < pkg->dsc.files.names.count ; i++ ) {
				fullfilename = calc_dirconcat(origdirectory,
						pkg->dsc.files.names.values[i]);
				if( fullfilename == NULL ) {
					r = RET_ERROR_OOM;
					break;
				}
				if( isregularfile(fullfilename) )
					deletefile(fullfilename);
				free(fullfilename);
			}
			free(origdirectory);
		}
		RET_ENDUPDATE(r, r2);
	}
	dsc_free(pkg, database);

	if( tracks != NULL ) {
		retvalue r2;
		r2 = trackingdata_finish(tracks, &trackingdata, database,
				dereferencedfilekeys);
		RET_ENDUPDATE(r,r2);
	}
	return r;
}
