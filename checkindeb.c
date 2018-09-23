/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2009,2012 Bernhard R. Link
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
#include <ctype.h>
#include "error.h"
#include "ignore.h"
#include "filecntl.h"
#include "strlist.h"
#include "checksums.h"
#include "names.h"
#include "checkindeb.h"
#include "reference.h"
#include "binaries.h"
#include "files.h"
#include "guesscomponent.h"
#include "tracking.h"
#include "override.h"
#include "hooks.h"

/* This file includes the code to include binaries, i.e.
   to create the chunk for the Packages.gz-file and
   to put it in the various databases.

Things to do with .deb's checkin by hand: (by comparison with apt-ftparchive)
- extract the control file (that's the hard part -> extractcontrol.c )
- check for Package, Version, Architecture, Maintainer, Description
- apply overwrite if necessary (section, priority and perhaps maintainer).
- add Size, MD5sum, Filename, Priority, Section
- remove Status (warning if existent?)
- check for Optional-field and reject then..
*/

struct debpackage {
	/* things to be set by deb_read: */
	struct deb_headers deb;
	/* things that will still be NULL then: */
	component_t component;
	/* with deb_calclocations: */
	const char *filekey;
	struct strlist filekeys;
};

void deb_free(/*@only@*/struct debpackage *pkg) {
	if (pkg != NULL) {
		binaries_debdone(&pkg->deb);
		if (pkg->filekey != NULL)
			strlist_done(&pkg->filekeys);
	}
	free(pkg);
}

/* read the data from a .deb, make some checks and extract some data */
static retvalue deb_read(/*@out@*/struct debpackage **pkg, const char *filename) {
	retvalue r;
	struct debpackage *deb;

	deb = zNEW(struct debpackage);
	if (FAILEDTOALLOC(deb))
		return RET_ERROR_OOM;

	r = binaries_readdeb(&deb->deb, filename);
	if (RET_IS_OK(r))
		r = properpackagename(deb->deb.name);
	if (RET_IS_OK(r))
		r = propersourcename(deb->deb.source);
	if (RET_IS_OK(r))
		r = properversion(deb->deb.sourceversion);
	if (RET_IS_OK(r))
		r = properversion(deb->deb.version);
	if (RET_WAS_ERROR(r)) {
		deb_free(deb);
		return r;
	}
	*pkg = deb;

	return RET_OK;
}

static retvalue deb_preparelocation(struct debpackage *pkg, component_t forcecomponent, const struct atomlist *forcearchitectures, const char *forcesection, const char *forcepriority, packagetype_t packagetype, struct distribution *distribution, const struct overridedata **oinfo_ptr, const char *debfilename){
	const struct atomlist *components;
	const struct overridefile *binoverride;
	const struct overridedata *oinfo;
	retvalue r;

	if (packagetype == pt_udeb) {
		binoverride = distribution->overrides.udeb;
		components = &distribution->udebcomponents;
	} else {
		binoverride = distribution->overrides.deb;
		components = &distribution->components;
	}

	oinfo = override_search(binoverride, pkg->deb.name);
	*oinfo_ptr = oinfo;
	if (forcesection == NULL) {
		forcesection = override_get(oinfo, SECTION_FIELDNAME);
	}
	if (forcepriority == NULL) {
		forcepriority = override_get(oinfo, PRIORITY_FIELDNAME);
	}
	if (!atom_defined(forcecomponent)) {
		const char *fc;

		fc = override_get(oinfo, "$Component");
		if (fc != NULL) {
			forcecomponent = component_find(fc);
			if (!atom_defined(forcecomponent)) {
				fprintf(stderr,
"Unparseable component '%s' in $Component override of '%s'\n",
					fc, pkg->deb.name);
				return RET_ERROR;
			}
		}
	}

	if (forcesection != NULL) {
		free(pkg->deb.section);
		pkg->deb.section = strdup(forcesection);
		if (FAILEDTOALLOC(pkg->deb.section)) {
			return RET_ERROR_OOM;
		}
	}
	if (forcepriority != NULL) {
		free(pkg->deb.priority);
		pkg->deb.priority = strdup(forcepriority);
		if (FAILEDTOALLOC(pkg->deb.priority)) {
			return RET_ERROR_OOM;
		}
	}

	if (pkg->deb.section == NULL) {
		fprintf(stderr, "No section given for '%s', skipping.\n",
				pkg->deb.name);
		return RET_ERROR;
	}
	if (pkg->deb.priority == NULL) {
		fprintf(stderr, "No priority given for '%s', skipping.\n",
				pkg->deb.name);
		return RET_ERROR;
	}
	if (strcmp(pkg->deb.section, "unknown") == 0 && verbose >= 0) {
		fprintf(stderr, "Warning: strange section '%s'!\n",
				pkg->deb.section);
	}

	/* decide where it has to go */

	r = guess_component(distribution->codename, components,
			pkg->deb.name, pkg->deb.section,
			forcecomponent, &pkg->component);
	if (RET_WAS_ERROR(r))
		return r;
	if (verbose > 0 && !atom_defined(forcecomponent)) {
		fprintf(stderr, "%s: component guessed as '%s'\n", debfilename,
				atoms_components[pkg->component]);
	}

	/* some sanity checks: */

	if (forcearchitectures != NULL &&
			pkg->deb.architecture != architecture_all &&
			!atomlist_in(forcearchitectures,
				pkg->deb.architecture)) {
		fprintf(stderr,
"Cannot add '%s', as it is architecture '%s' and you specified to only include ",
				debfilename,
				atoms_architectures[pkg->deb.architecture]);
		atomlist_fprint(stderr, at_architecture, forcearchitectures);
		fputs(".\n", stderr);
		return RET_ERROR;
	} else if (pkg->deb.architecture != architecture_all &&
			!atomlist_in(&distribution->architectures,
				pkg->deb.architecture)) {
		(void)fprintf(stderr,
"Error looking at '%s': '%s' is not one of the valid architectures: '",
				debfilename,
				atoms_architectures[pkg->deb.architecture]);
		(void)atomlist_fprint(stderr, at_architecture,
				&distribution->architectures);
		(void)fputs("'\n", stderr);
		return RET_ERROR;
	}
	if (!atomlist_in(components, pkg->component)) {
		fprintf(stderr,
"Error looking at %s': Would be placed in unavailable component '%s'!\n",
				debfilename,
				atoms_components[pkg->component]);
		/* this cannot be ignored
		 * as there is not data structure available */
		return RET_ERROR;
	}

	r = binaries_calcfilekeys(pkg->component, &pkg->deb,
			packagetype, &pkg->filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	pkg->filekey = pkg->filekeys.values[0];
	return RET_OK;
}


retvalue deb_prepare(/*@out@*/struct debpackage **deb, component_t forcecomponent, architecture_t forcearchitecture, const char *forcesection, const char *forcepriority, packagetype_t packagetype, struct distribution *distribution, const char *debfilename, const char * const givenfilekey, const struct checksums * checksums, const struct strlist *allowed_binaries, const char *expectedsourcepackage, const char *expectedsourceversion){
	retvalue r;
	struct debpackage *pkg;
	const struct overridedata *oinfo;
	char *control;
	struct atomlist forcearchitectures;
	const char *packagenametocheck;
	char *base;
	size_t l;

	assert (givenfilekey != NULL);
	assert (checksums != NULL);
	assert (allowed_binaries != NULL);
	assert (expectedsourcepackage != NULL);
	assert (expectedsourceversion != NULL);

	/* First taking a closer look in the file: */

	r = deb_read(&pkg, debfilename);
	if (RET_WAS_ERROR(r)) {
		return r;
	}

	/* -dbgsym packages are not listed in the Binary header, so look
	 * for the base name instead */
	packagenametocheck = pkg->deb.name;
	l = strlen(pkg->deb.name);
	if (l > sizeof("-dbgsym")-1 &&
	    strcmp(pkg->deb.name + l - (sizeof("dbgsym")), "-dbgsym") == 0) {
		base = strndup(pkg->deb.name, l - (sizeof("dbgsym")));
		if (FAILEDTOALLOC(base)) {
			deb_free(pkg);
			return RET_ERROR_OOM;
		}
		packagenametocheck = base;
	} else {
		base = NULL;
	}

	if (!strlist_in(allowed_binaries, packagenametocheck) &&
	    !IGNORING(surprisingbinary,
"'%s' has packagename '%s' not listed in the .changes file!\n",
					debfilename, packagenametocheck)) {
		deb_free(pkg);
		free(base);
		return RET_ERROR;
	}
	free(base);
	if (strcmp(pkg->deb.source, expectedsourcepackage) != 0) {
		/* this cannot be ignored easily, as it determines
		 * the directory this file is stored into */
	    fprintf(stderr,
"'%s' lists source package '%s', but .changes says it is '%s'!\n",
				debfilename, pkg->deb.source,
				expectedsourcepackage);
		deb_free(pkg);
		return RET_ERROR;
	}
	if (strcmp(pkg->deb.sourceversion, expectedsourceversion) != 0 &&
	    !IGNORING(wrongsourceversion,
"'%s' lists source version '%s', but .changes says it is '%s'!\n",
				debfilename, pkg->deb.sourceversion,
				expectedsourceversion)) {
		deb_free(pkg);
		return RET_ERROR;
	}

	forcearchitectures.count = 1;
	forcearchitectures.size = 1;
	forcearchitectures.atoms = &forcearchitecture;

	r = deb_preparelocation(pkg, forcecomponent, &forcearchitectures,
			forcesection, forcepriority, packagetype, distribution,
			&oinfo, debfilename);
	if (RET_WAS_ERROR(r)) {
		deb_free(pkg);
		return r;
	}

	if (strcmp(givenfilekey, pkg->filekey) != 0) {
		fprintf(stderr,
"Name mismatch: .changes indicates '%s', but the file itself says '%s'!\n",
				givenfilekey, pkg->filekey);
		deb_free(pkg);
		return RET_ERROR;
	}
	/* Prepare everything that can be prepared beforehand */
	r = binaries_complete(&pkg->deb, pkg->filekey, checksums, oinfo,
			pkg->deb.section, pkg->deb.priority, &control);
	if (RET_WAS_ERROR(r)) {
		deb_free(pkg);
		return r;
	}
	free(pkg->deb.control); pkg->deb.control = control;
	*deb = pkg;
	return RET_OK;
}

retvalue deb_addprepared(const struct debpackage *pkg, const struct atomlist *forcearchitectures, packagetype_t packagetype, struct distribution *distribution, struct trackingdata *trackingdata) {
	return binaries_adddeb(&pkg->deb, forcearchitectures,
			packagetype, distribution, trackingdata,
			pkg->component, &pkg->filekeys,
			pkg->deb.control);
}

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <d->architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if component is NULL, guessing it from the section. */
retvalue deb_add(component_t forcecomponent, const struct atomlist *forcearchitectures, const char *forcesection, const char *forcepriority, packagetype_t packagetype, struct distribution *distribution, const char *debfilename, int delete, /*@null@*/trackingdb tracks) {
	struct debpackage *pkg;
	retvalue r;
	struct trackingdata trackingdata;
	const struct overridedata *oinfo;
	char *control;
	struct checksums *checksums;

	causingfile = debfilename;

	r = deb_read(&pkg, debfilename);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	r = deb_preparelocation(pkg, forcecomponent, forcearchitectures,
			forcesection, forcepriority, packagetype, distribution,
			&oinfo, debfilename);
	if (RET_WAS_ERROR(r)) {
		deb_free(pkg);
		return r;
	}
	r = files_preinclude(debfilename, pkg->filekey, &checksums);
	if (RET_WAS_ERROR(r)) {
		deb_free(pkg);
		return r;
	}
	/* Prepare everything that can be prepared beforehand */
	r = binaries_complete(&pkg->deb, pkg->filekey, checksums, oinfo,
			pkg->deb.section, pkg->deb.priority, &control);
	checksums_free(checksums);
	if (RET_WAS_ERROR(r)) {
		deb_free(pkg);
		return r;
	}
	free(pkg->deb.control); pkg->deb.control = control;

	if (tracks != NULL) {
		assert(pkg->deb.sourceversion != NULL);
		r = trackingdata_summon(tracks,
				pkg->deb.source, pkg->deb.sourceversion,
				&trackingdata);
		if (RET_WAS_ERROR(r)) {
			deb_free(pkg);
			return r;
		}
	}

	r = binaries_adddeb(&pkg->deb, forcearchitectures,
			packagetype, distribution,
			(tracks!=NULL)?&trackingdata:NULL,
			pkg->component, &pkg->filekeys,
			pkg->deb.control);
	RET_UPDATE(distribution->status, r);
	deb_free(pkg);

	if (tracks != NULL) {
		retvalue r2;
		r2 = trackingdata_finish(tracks, &trackingdata);
		RET_ENDUPDATE(r, r2);
	}

	if (RET_IS_OK(r) && delete >= D_MOVE) {
		deletefile(debfilename);
	} else if (r == RET_NOTHING && delete >= D_DELETE)
		deletefile(debfilename);

	return r;
}
