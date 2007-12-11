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
#include "ignore.h"
#include "strlist.h"
#include "md5sum.h"
#include "names.h"
#include "chunks.h"
#include "checkindeb.h"
#include "reference.h"
#include "binaries.h"
#include "files.h"
#include "guesscomponent.h"
#include "tracking.h"
#include "override.h"

extern int verbose;

/* This file includes the code to include binaries, i.e.
   to create the chunk for the Packages.gz-file and
   to put it in the various databases.

Things to do with .deb's checkin by hand: (by comparison with apt-ftparchive)
- extract the control file (that's the hard part -> extractcontrol.c )
- check for Package, Version, Architecture, Maintainer, Description
- apply overwrite if neccesary (section,priority and perhaps maintainer).
- add Size, MD5sum, Filename, Priority, Section
- remove Status (warning if existant?)
- check for Optional-field and reject then..
*/

struct debpackage {
	/* things to be set by deb_read: */
	struct deb_headers deb;
	/* things that will still be NULL then: */
	char *component; //This might be const, too and save some strdups, but...
	/* with deb_calclocations: */
	const char *filekey;
	struct strlist filekeys;
	/* with deb_copyfiles or deb_checkfiles: */
	char *md5sum;
};

void deb_free(/*@only@*/struct debpackage *pkg) {
	if( pkg != NULL ) {
		binaries_debdone(&pkg->deb);
		free(pkg->component);
		if( pkg->filekey != NULL )
			strlist_done(&pkg->filekeys);
		free(pkg->md5sum);
	}
	free(pkg);
}

/* read the data from a .deb, make some checks and extract some data */
static retvalue deb_read(/*@out@*/struct debpackage **pkg, const char *filename, bool needssourceversion) {
	retvalue r;
	struct debpackage *deb;

	deb = calloc(1,sizeof(struct debpackage));

	r = binaries_readdeb(&deb->deb, filename, needssourceversion);
	if( RET_IS_OK(r) )
		r = properpackagename(deb->deb.name);
	if( RET_IS_OK(r) )
		r = propersourcename(deb->deb.source);
	if( RET_IS_OK(r) && needssourceversion )
		r = properversion(deb->deb.sourceversion);
	if( RET_IS_OK(r) )
		r = properversion(deb->deb.version);
	if( !RET_WAS_ERROR(r) )
		r = properfilenamepart(deb->deb.architecture);
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	*pkg = deb;

	return RET_OK;
}

retvalue deb_prepare(/*@out@*/struct debpackage **deb, struct database *database, const char * const forcecomponent, const char * const forcearchitecture, const char *forcesection, const char *forcepriority, const char * const packagetype, struct distribution *distribution, const char *debfilename, const char * const givenfilekey, const char * const givenmd5sum, int delete, bool needsourceversion, const struct strlist *allowed_binaries, const char *expectedsourcepackage, const char *expectedsourceversion){
	retvalue r;
	struct debpackage *pkg;
	const struct overrideinfo *oinfo;
	const struct strlist *components;
	const struct overrideinfo *binoverride;
	char *control;

	assert( givenmd5sum!=NULL ||
		(givenmd5sum==NULL && givenfilekey==NULL ) );

	/* First taking a closer look to the file: */

	r = deb_read(&pkg,debfilename,
			needsourceversion || expectedsourceversion != NULL);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	if( allowed_binaries != NULL &&
	    !strlist_in(allowed_binaries, pkg->deb.name) &&
	    !IGNORING_(surprisingbinary,
"'%s' has packagename '%s' not listed in the .changes file!\n",
					debfilename, pkg->deb.name)) {
		deb_free(pkg);
		return RET_ERROR;
	}
	if( expectedsourcepackage != NULL &&
	    strcmp(pkg->deb.source, expectedsourcepackage) != 0 ) {
		/* this cannot ne ignored easily, as it determines
		 * the directory this file is stored into */
	    fprintf(stderr,
"'%s' lists source package '%s', but .changes says it is '%s'!\n",
				debfilename, pkg->deb.source,
				expectedsourcepackage);
		deb_free(pkg);
		return RET_ERROR;
	}
	if( expectedsourceversion != NULL &&
	    strcmp(pkg->deb.sourceversion, expectedsourceversion) != 0 &&
	    !IGNORING_(wrongsourceversion,
"'%s' lists source version '%s', but .changes says it is '%s'!\n",
				debfilename, pkg->deb.sourceversion,
				expectedsourceversion)) {
		deb_free(pkg);
		return RET_ERROR;
	}

	if( strcmp(packagetype,"udeb") == 0 ) {
		binoverride = distribution->overrides.udeb;
		components = &distribution->udebcomponents;
	} else {
		binoverride = distribution->overrides.deb;
		components = &distribution->components;
	}

	oinfo = override_search(binoverride,pkg->deb.name);
	if( forcesection == NULL ) {
		forcesection = override_get(oinfo,SECTION_FIELDNAME);
	}
	if( forcepriority == NULL ) {
		forcepriority = override_get(oinfo,PRIORITY_FIELDNAME);
	}

	if( forcesection != NULL ) {
		free(pkg->deb.section);
		pkg->deb.section = strdup(forcesection);
		if( pkg->deb.section == NULL ) {
			deb_free(pkg);
			return RET_ERROR_OOM;
		}
	}
	if( forcepriority != NULL ) {
		free(pkg->deb.priority);
		pkg->deb.priority = strdup(forcepriority);
		if( pkg->deb.priority == NULL ) {
			deb_free(pkg);
			return RET_ERROR_OOM;
		}
	}

	if( pkg->deb.section == NULL ) {
		fprintf(stderr,"No section was given for '%s', skipping.\n",
				pkg->deb.name);
		deb_free(pkg);
		return RET_ERROR;
	}
	if( pkg->deb.priority == NULL ) {
		fprintf(stderr,"No priority was given for '%s', skipping.\n",
				pkg->deb.name);
		deb_free(pkg);
		return RET_ERROR;
	}

	/* decide where it has to go */

	r = guess_component(distribution->codename, components,
			pkg->deb.name, pkg->deb.section,
			forcecomponent, &pkg->component);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	}
	if( verbose > 0 && forcecomponent == NULL ) {
		fprintf(stderr,"%s: component guessed as '%s'\n",debfilename,pkg->component);
	}

	/* some sanity checks: */

	if( forcearchitecture != NULL && strcmp(forcearchitecture,"all") != 0 &&
			strcmp(pkg->deb.architecture,forcearchitecture) != 0 &&
			strcmp(pkg->deb.architecture,"all") != 0 ) {
		fprintf(stderr,"Cannot checking in '%s' into architecture '%s', as it is '%s'!\n",
				debfilename,forcearchitecture,pkg->deb.architecture);
		deb_free(pkg);
/* TODO: this should be moved upwards...
		if( delete >= D_DELETE ) {
			if( verbose >= 0 )
				fprintf(stderr,"Deleting '%s' as requested!\n",debfilename);
			if( unlink(debfilename) != 0 ) {
				fprintf(stderr,"Error deleting '%s': %m\n",debfilename);
			}
		}
*/
		return RET_ERROR;
	} else if( strcmp(pkg->deb.architecture,"all") != 0 &&
	    !strlist_in( &distribution->architectures, pkg->deb.architecture )) {
		(void)fprintf(stderr,"While checking in '%s': '%s' is not listed in '",
				debfilename, pkg->deb.architecture);
		(void)strlist_fprint(stderr, &distribution->architectures);
		(void)fputs("'\n",stderr);
		deb_free(pkg);
		return RET_ERROR;
	}
	if( !strlist_in(components,pkg->component) ) {
		fprintf(stderr,"While checking in '%s': Would put in component '%s', but that is not available!\n",debfilename,pkg->component);
		/* this cannot be ignored as there is not data structure available*/
		return RET_ERROR;
	}

	r = binaries_calcfilekeys(pkg->component, &pkg->deb, packagetype, &pkg->filekeys);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	}
	pkg->filekey = pkg->filekeys.values[0];

	if( givenfilekey!=NULL && strcmp(givenfilekey,pkg->filekey) != 0 ) {
		fprintf(stderr,"Name mismatch, .changes indicates '%s', but the file itself says '%s'!\n",givenfilekey,pkg->filekey);
		deb_free(pkg);
		return RET_ERROR;
	}
	/* then looking if we already have this, or copy it in */
	if( givenmd5sum != NULL ) {
		pkg->md5sum = strdup(givenmd5sum);
		if( pkg->md5sum == NULL ) {
			deb_free(pkg);
			return RET_ERROR_OOM;
		}
		if( givenfilekey == NULL ) {
			r = files_ready(database, pkg->filekey,pkg->md5sum);
			if( RET_WAS_ERROR(r) ) {
				deb_free(pkg);
				return r;
			}
		}
	} else {
		assert(givenfilekey == NULL);
		r = files_include(database,debfilename,pkg->filekey,NULL,&pkg->md5sum,delete);
		if( RET_WAS_ERROR(r) ) {
			deb_free(pkg);
			return r;
		}

	}
	assert( pkg->md5sum != NULL );
	/* Prepare everything that can be prepared beforehand */
	r = binaries_complete(&pkg->deb, pkg->filekey, pkg->md5sum, oinfo,
			pkg->deb.section, pkg->deb.priority, &control);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	}
	free(pkg->deb.control); pkg->deb.control = control;
	*deb = pkg;
	return RET_OK;
}

retvalue deb_hardlinkfiles(struct debpackage *deb,struct database *database,const char *debfilename) {
	assert( deb != NULL );
	assert( deb->filekey != NULL && deb-> md5sum != NULL );
	return files_hardlink(database, debfilename, deb->filekey, deb->md5sum);
}

retvalue deb_addprepared(const struct debpackage *pkg,struct database *database,const char *forcearchitecture,const char *packagetype,struct distribution *distribution,struct strlist *dereferencedfilekeys,struct trackingdata *trackingdata) {
	return binaries_adddeb(&pkg->deb, database, forcearchitecture,
			packagetype, distribution, dereferencedfilekeys,
			trackingdata,
			pkg->component, &pkg->filekeys, pkg->deb.control);
}

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <d->architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if component is NULL, guessing it from the section. */
retvalue deb_add(struct database *database,const char *forcecomponent,const char *forcearchitecture,const char *forcesection,const char *forcepriority,const char *packagetype,struct distribution *distribution,const char *debfilename,int delete,struct strlist *dereferencedfilekeys,/*@null@*/trackingdb tracks){
	struct debpackage *pkg;
	retvalue r;
	struct trackingdata trackingdata;

	r = deb_prepare(&pkg,database,forcecomponent,forcearchitecture,forcesection,forcepriority,packagetype,distribution,debfilename,NULL,NULL,delete,tracks!=NULL,NULL,NULL,NULL);
	if( RET_WAS_ERROR(r) )
		return r;

	if( tracks != NULL ) {
		assert(pkg->deb.sourceversion != NULL);
		r = trackingdata_summon(tracks,
				pkg->deb.source, pkg->deb.sourceversion,
				&trackingdata);
		if( RET_WAS_ERROR(r) ) {
			deb_free(pkg);
			return r;
		}
	}

	r = binaries_adddeb(&pkg->deb, database, forcearchitecture,
			packagetype, distribution, dereferencedfilekeys,
			(tracks!=NULL)?&trackingdata:NULL,
			pkg->component, &pkg->filekeys, pkg->deb.control);
	RET_UPDATE(distribution->status, r);
	deb_free(pkg);

	if( tracks != NULL ) {
		retvalue r2;
		r2 = trackingdata_finish(tracks, &trackingdata, database, dereferencedfilekeys);
		RET_ENDUPDATE(r,r2);
	}

	return r;
}
