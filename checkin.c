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
#include "strlist.h"
#include "mprintf.h"
#include "atoms.h"
#include "checksums.h"
#include "names.h"
#include "filecntl.h"
#include "dirs.h"
#include "chunks.h"
#include "reference.h"
#include "signature.h"
#include "sources.h"
#include "files.h"
#include "tracking.h"
#include "guesscomponent.h"
#include "override.h"
#include "checkindsc.h"
#include "checkindeb.h"
#include "checkin.h"
#include "uploaderslist.h"
#include "log.h"
#include "dpkgversions.h"
#include "changes.h"

/* Things to do when including a .changes-file:
 *  - Read in the chunk of the possible signed file.
 *    (In later versions possibly checking the signature)
 *  - Parse it, extracting:
 *	+ Distribution
 * 	+ Source
 * 	+ Architecture
 * 	+ Binary
 * 	+ Version
 * 	+ ...
 * 	+ Files
 *  - Calculate what files are expectable...
 *  - Compare supplied filed with files expected.
 *  - (perhaps: write what was done and changes to some logfile)
 *  - add supplied files to the pool and register them in files.db
 *  - add the .dsc-files via checkindsc.c
 *  - add the .deb-filed via checkindeb.c
 *
 */

struct fileentry {
	struct fileentry *next;
	char *basename;
	filetype type;
	struct checksums *checksums;
	char *section;
	char *priority;
	architecture_t architecture_into;
	char *name;
	/* this might be different for different files,
	 * (though this is only allowed in rare cases),
	 * will be set by _fixfields */
	component_t component;
	/* only set after changes_includefiles */
	char *filekey;
	/* was already found in the pool before */
	bool wasalreadythere;
	/* set between checkpkg and includepkg */
	struct strlist needed_filekeys;
	union { struct dsc_headers dsc;
		struct debpackage *deb;} pkg;
	/* only valid while parsing: */
	struct hashes hashes;
};

struct changes {
	/* Things read by changes_read: */
	char *source, *sourceversion, *changesversion;
	struct strlist distributions,
		       architectures,
		       binaries;
	struct fileentry *files;
	char *control;
	struct signatures *signatures;
	/* Things to be set by changes_fixfields: */
	/* the component source files are put into */
	component_t srccomponent;
	/* != NULL if changesfile was put into pool/ */
	/*@null@*/ char *changesfilekey;
	/* the directory where source files are put into */
	char *srcdirectory;
	/* (only to warn if multiple are used) */
	component_t firstcomponent;
	/* the directory the .changes file resides in */
	char *incomingdirectory;
	/* the Version: and the version in Source: differ */
	bool isbinnmu;
};

static void freeentries(/*@only@*/struct fileentry *entry) {
	struct fileentry *h;

	while (entry != NULL) {
		h = entry->next;
		free(entry->filekey);
		free(entry->basename);
		checksums_free(entry->checksums);
		free(entry->section);
		free(entry->priority);
		free(entry->name);
		if (FE_BINARY(entry->type))
			deb_free(entry->pkg.deb);
		 else if (entry->type == fe_DSC) {
			 strlist_done(&entry->needed_filekeys);
			 sources_done(&entry->pkg.dsc);
		 }
		free(entry);
		entry = h;
	}
}

static void changes_free(/*@only@*/struct changes *changes) {
	if (changes != NULL) {
		free(changes->source);
		free(changes->sourceversion);
		free(changes->changesversion);
		strlist_done(&changes->architectures);
		strlist_done(&changes->binaries);
		freeentries(changes->files);
		strlist_done(&changes->distributions);
		free(changes->control);
		free(changes->srcdirectory);
		free(changes->changesfilekey);
//		trackedpackage_free(changes->trackedpkg);
		free(changes->incomingdirectory);
		signatures_free(changes->signatures);
	}
	free(changes);
}


static retvalue newentry(struct fileentry **entry, const char *fileline, const struct atomlist *packagetypes, const struct atomlist *forcearchitectures, const char *sourcename, bool includebyhand, bool includelogs, bool includebuildinfos, bool *ignoredlines_p, bool skip_binaries) {
	struct fileentry *e;
	retvalue r;

	e = zNEW(struct fileentry);
	if (FAILEDTOALLOC(e))
		return RET_ERROR_OOM;

	r = changes_parsefileline(fileline, &e->type, &e->basename,
			&e->hashes.hashes[cs_md5sum],
			&e->hashes.hashes[cs_length],
			&e->section, &e->priority, &e->architecture_into,
			&e->name);
	if (RET_WAS_ERROR(r)) {
		free(e);
		return r;
	}
	assert (RET_IS_OK(r));
	if (e->type == fe_BYHAND) {
		if (!includebyhand) {
			// TODO: at least check them and fail if wrong?
			fprintf(stderr, "Ignoring byhand file: '%s'!\n",
							e->basename);
			freeentries(e);
			*ignoredlines_p = true;
			return RET_NOTHING;
		}
		e->next = *entry;
		*entry = e;
		return RET_OK;
	}
	if (FE_SOURCE(e->type) && limitations_missed(packagetypes, pt_dsc)) {
		freeentries(e);
		*ignoredlines_p = true;
		return RET_NOTHING;
	}
	if (e->type == fe_DEB && limitations_missed(packagetypes, pt_deb)) {
		freeentries(e);
		*ignoredlines_p = true;
		return RET_NOTHING;
	}
	if (e->type == fe_UDEB && limitations_missed(packagetypes, pt_udeb)) {
		freeentries(e);
		*ignoredlines_p = true;
		return RET_NOTHING;
	}
	if (e->type == fe_DDEB && limitations_missed(packagetypes, pt_ddeb)) {
		freeentries(e);
		*ignoredlines_p = true;
		return RET_NOTHING;
	}
	if (e->type != fe_LOG && e->type != fe_BUILDINFO &&
			e->architecture_into == architecture_source &&
			strcmp(e->name, sourcename) != 0) {
		fprintf(stderr,
"Warning: File '%s' looks like source but does not start with '%s_'!\n",
				e->basename, sourcename);
	} else if (e->type == fe_BUILDINFO) {
		if (strcmp(e->name, sourcename) != 0) {
			fprintf(stderr,
"Warning: File '%s' looks like buildinfo but does not start with '%s_'!\n",
					e->basename, sourcename);
		}
		if (!includebuildinfos) {
			if (verbose > 2)
				fprintf(stderr,
"Ignoring buildinfo file: '%s'!\n", e->basename);
			freeentries(e);
			*ignoredlines_p = true;
			return RET_NOTHING;
		}
		/* otherwise the normal rules like for .deb, see below */
	}
	if (e->type == fe_LOG) {
		if (strcmp(e->name, sourcename) != 0) {
			fprintf(stderr,
"Warning: File '%s' looks like log but does not start with '%s_'!\n",
					e->basename, sourcename);
		}
		if (!includelogs) {
			// TODO: at least check them and fail if wrong?
			fprintf(stderr, "Ignoring log file: '%s'!\n",
							e->basename);
			freeentries(e);
			*ignoredlines_p = true;
			return RET_NOTHING;
		}
		/* a log file without parseable architecture (atom undefined)
		 * might still belong to an forced architecture (as it might
		 * list multiples), so cannot be excluded here: */
		if (atom_defined(e->architecture_into) &&
				limitations_missed(forcearchitectures,
					e->architecture_into)) {
			if (verbose > 1)
				fprintf(stderr,
"Skipping '%s' as not for architecture ",
					e->basename);
			atomlist_fprint(stderr, at_architecture,
					forcearchitectures);
			fputs(".\n", stderr);
			freeentries(e);
			*ignoredlines_p = true;
			return RET_NOTHING;
		}
	} else if (forcearchitectures != NULL) {
		if (e->architecture_into == architecture_all &&
				!skip_binaries) {
			if (verbose > 2) {
				fprintf(stderr,
"Limiting '%s' to architectures ",
					e->basename);
				atomlist_fprint(stderr, at_architecture,
						forcearchitectures);
				fputs(" as requested.\n", stderr);
			}
			/* keep e->architecture_into to all, this wil
			 * be restricted to forcearchitectures when added */
		} else if (!atomlist_in(forcearchitectures,
					e->architecture_into)) {
			if (verbose > 1) {
				if (atom_defined(e->architecture_into))
					fprintf(stderr,
"Skipping '%s' as architecture '%s' is not in the requested set.\n",
					e->basename,
					atoms_architectures[
					 e->architecture_into]);
				else
					fprintf(stderr,
"Skipping '%s' as architecture is not in the requested set.\n",
					e->basename);
			}
			freeentries(e);
			*ignoredlines_p = true;
			return RET_NOTHING;
		}
	}

	e->next = *entry;
	*entry = e;
	return RET_OK;
}

/* Parse the Files-header to see what kind of files we carry around */
static retvalue changes_parsefilelines(const char *filename, struct changes *changes, const struct strlist *filelines, const struct atomlist *packagetypes, const struct atomlist *forcearchitectures, bool includebyhand, bool includelogs, bool includebuildinfos, bool *ignoredlines_p, bool skip_binaries) {
	retvalue result, r;
	int i;

	assert (changes->files == NULL);
	result = RET_NOTHING;

	for (i = 0 ; i < filelines->count ; i++) {
		const char *fileline = filelines->values[i];

		r = newentry(&changes->files, fileline,
				packagetypes, forcearchitectures,
				changes->source,
				includebyhand, includelogs, includebuildinfos,
				ignoredlines_p, skip_binaries);
		RET_UPDATE(result, r);
		if (r == RET_ERROR)
			return r;
	}
	if (result == RET_NOTHING) {
		fprintf(stderr,
"%s: Not enough files in .changes!\n", filename);
		return RET_ERROR;
	}
	return result;
}

static retvalue changes_addhashes(const char *filename, struct changes *changes, enum checksumtype cs, struct strlist *filelines, bool ignoresomefiles) {
	int i;
	retvalue r;

	for (i = 0 ; i < filelines->count ; i++) {
		struct hash_data data, size;
		const char *fileline = filelines->values[i];
		struct fileentry *e;
		const char *basefilename;

		r = hashline_parse(filename, fileline, cs, &basefilename,
				&data, &size);
		if (r == RET_NOTHING)
			continue;
		if (RET_WAS_ERROR(r))
			return r;
		e = changes->files;
		while (e != NULL && strcmp(e->basename, basefilename) != 0)
			e = e->next;
		if (e == NULL) {
			if (ignoresomefiles)
				/* we might already have ignored files when
				 * creating changes->files, so we cannot say
				 * if this is an error. */
				continue;
			fprintf(stderr,
"In '%s': file '%s' listed in '%s' but not in 'Files'\n",
				filename, basefilename,
				changes_checksum_names[cs]);
			return RET_ERROR;
		}
		if (e->hashes.hashes[cs_length].len != size.len ||
				memcmp(e->hashes.hashes[cs_length].start,
					size.start, size.len) != 0) {
			fprintf(stderr,
"In '%s': file '%s' listed in '%s' with different size than in 'Files'\n",
				filename, basefilename,
				changes_checksum_names[cs]);
			return RET_ERROR;
		}
		e->hashes.hashes[cs] = data;
	}
	return RET_OK;
}

static retvalue changes_finishhashes(struct changes *changes) {
	struct fileentry *e;
	retvalue r;

	for (e = changes->files ; e != NULL ; e = e->next) {
		r = checksums_initialize(&e->checksums, e->hashes.hashes);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}


static retvalue check(const char *filename, struct changes *changes, const char *field) {
	retvalue r;

	r = chunk_checkfield(changes->control, field);
	if (r == RET_NOTHING) {
		if (IGNORING(missingfield,
"In '%s': Missing '%s' field!\n", filename, field)) {
			return RET_OK;
		} else {
			return RET_ERROR;
		}
	}
	return r;
}

static retvalue changes_read(const char *filename, /*@out@*/struct changes **changes, const struct atomlist *packagetypes, const struct atomlist *forcearchitectures, bool includebyhand, bool includelogs, bool includebuildinfos) {
	retvalue r;
	struct changes *c;
	struct strlist filelines[cs_hashCOUNT];
	enum checksumtype cs;
	bool broken, ignoredlines;
	int versioncmp;
	bool skip_binaries;

#define E(err) { \
		if (r == RET_NOTHING) { \
			fprintf(stderr, "In '%s': " err "\n", filename); \
			r = RET_ERROR; \
		} \
		if (RET_WAS_ERROR(r)) { \
			changes_free(c); \
			return r; \
		} \
	}
#define R { \
		if (RET_WAS_ERROR(r)) { \
			changes_free(c); \
			return r; \
		} \
	}


	c = zNEW(struct changes);
	if (FAILEDTOALLOC(c))
		return RET_ERROR_OOM;
	r = signature_readsignedchunk(filename, filename,
			&c->control, &c->signatures, &broken);
	R;
	if (broken && !IGNORING(brokensignatures,
"'%s' contains only broken signatures.\n"
"This most likely means the file was damaged or edited improperly.\n",
				filename)) {
		r = RET_ERROR;
		R;
	}
	r = check(filename, c, "Format");
	R;
	r = check(filename, c, "Date");
	R;
	r = chunk_getnameandversion(c->control, "Source",
			&c->source, &c->sourceversion);
	E("Missing 'Source' field");
	r = propersourcename(c->source);
	R;
	if (c->sourceversion != NULL) {
		r = properversion(c->sourceversion);
		R;
	}
	r = chunk_getwordlist(c->control, "Architecture", &c->architectures);
	E("Missing 'Architecture' field");
	r = chunk_getwordlist(c->control, "Binary", &c->binaries);
	if (r == RET_NOTHING) {
		/* this could print a waring if architectures
		 * contains anything but 'source', but the .deb
		 * files are checked anyway... */
		strlist_init(&c->binaries);
	}
	R;
	r = chunk_getvalue(c->control, "Version", &c->changesversion);
	E("Missing 'Version' field");
	r = properversion(c->changesversion);
	E("Malforce Version number");
	if (c->sourceversion == NULL) {
		c->sourceversion = strdup(c->changesversion);
		if (FAILEDTOALLOC(c->sourceversion)) {
			changes_free(c);
			return RET_ERROR_OOM;
		}
		c->isbinnmu = false;
	} else {
		r = dpkgversions_cmp(c->sourceversion, c->changesversion,
				&versioncmp);
		E("Error comparing versions. (That should have been caught earlier, why now?)");
		c->isbinnmu = versioncmp != 0;
	}
	r = chunk_getwordlist(c->control, "Distribution", &c->distributions);
	E("Missing 'Distribution' field");
	r = check(filename, c, "Maintainer");
	R;
	r = chunk_getextralinelist(c->control,
			changes_checksum_names[cs_md5sum],
			&filelines[cs_md5sum]);
	E("Missing 'Files' field!");
	ignoredlines = false;
	/* check if forcearchitectures allows non-source binaries,
	 * (used to check if Architecture all are skipped) */
	if (forcearchitectures == NULL) {
		skip_binaries = false;
	} else {
		skip_binaries = !atomlist_hasexcept(forcearchitectures,
				architecture_source);
	}
	r = changes_parsefilelines(filename, c, &filelines[cs_md5sum],
			packagetypes, forcearchitectures,
			includebyhand, includelogs, includebuildinfos,
			&ignoredlines, skip_binaries);
	if (RET_WAS_ERROR(r)) {
		strlist_done(&filelines[cs_md5sum]);
		changes_free(c);
		return r;
	}
	for (cs = cs_firstEXTENDED ; cs < cs_hashCOUNT ; cs++) {
		r = chunk_getextralinelist(c->control,
				changes_checksum_names[cs], &filelines[cs]);
		if (RET_IS_OK(r))
			r = changes_addhashes(filename, c, cs, &filelines[cs],
					ignoredlines);
		else
			strlist_init(&filelines[cs]);
		if (RET_WAS_ERROR(r)) {
			while (cs-- > cs_md5sum)
				strlist_done(&filelines[cs]);
			changes_free(c);
			return r;
		}
	}
	r = changes_finishhashes(c);
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++)
		strlist_done(&filelines[cs]);
	R;
	r = dirs_getdirectory(filename, &c->incomingdirectory);
	R;

	*changes = c;
	return RET_OK;
#undef E
#undef R
}

static retvalue changes_fixfields(const struct distribution *distribution, const char *filename, struct changes *changes, component_t forcecomponent, /*@null@*/const char *forcesection, /*@null@*/const char *forcepriority) {
	struct fileentry *e;
	retvalue r;
	bool needsourcedir = false;
	struct fileentry *needs_source_package = NULL;
	bool has_source_package = false;

	r = propersourcename(changes->source);
	if (RET_WAS_ERROR(r))
		return r;

	e = changes->files;
	if (e == NULL) {
		fprintf(stderr, "No files given in '%s'!\n", filename);
		return RET_ERROR;
	}

	for (; e != NULL ; e = e->next) {
		const struct overridedata *oinfo = NULL;
		const char *force = NULL;

		if (e->type == fe_BYHAND ||
		    e->type == fe_BUILDINFO ||
		    e->type == fe_LOG) {
			needsourcedir = true;
			continue;
		}

		/* section and priority are only needed for the dsc,
		 * not for the other source files */
		if (FE_SOURCE(e->type) && !FE_PACKAGE(e->type)) {
			needs_source_package = e;
			continue;
		}

		if (forcesection == NULL || forcepriority == NULL) {
			oinfo = override_search(
			FE_BINARY(e->type)?(e->type==fe_UDEB?
					distribution->overrides.udeb
					:distribution->overrides.deb)
				:distribution->overrides.dsc,
					e->name);
		}

		if (forcesection != NULL)
			force = forcesection;
		else
			force = override_get(oinfo, SECTION_FIELDNAME);
		if (force != NULL) {
			free(e->section);
			e->section = strdup(force);
			if (FAILEDTOALLOC(e->section))
				return RET_ERROR_OOM;
		}
		if (strcmp(e->section, "unknown") == 0 && verbose >= 0) {
			fprintf(stderr, "Warning: '%s' contains strange section '%s'!\n",
					filename, e->section);
		}
		if (strcmp(e->section, "-") == 0) {
			fprintf(stderr,
"No section specified for '%s' in '%s'!\n", e->basename, filename);
			return RET_ERROR;
		}
		if (forcepriority != NULL)
			force = forcepriority;
		else
			force = override_get(oinfo, PRIORITY_FIELDNAME);
		if (force != NULL) {
			free(e->priority);
			e->priority = strdup(force);
			if (FAILEDTOALLOC(e->priority))
				return RET_ERROR_OOM;
		}
		if (strcmp(e->priority, "-") == 0) {
			fprintf(stderr,
"No priority specified for '%s'!\n", filename);
			return RET_ERROR;
		}
		if (!atom_defined(forcecomponent)) {
			const char *fc;

			fc = override_get(oinfo, "$Component");
			if (fc != NULL) {
				forcecomponent = component_find(fc);
				if (!atom_defined(forcecomponent)) {
					fprintf(stderr,
"Unparseable component '%s' in $Component override of '%s'\n",
						fc, e->name);
					return RET_ERROR;
				}
			}
		}

		// I'm undecided here. If this is a udeb, one could also use
		// distribution->udebcomponents. Though this might result
		// in not really predictable guesses for the section.
		r = guess_component(distribution->codename,
				&distribution->components, changes->source,
				e->section, forcecomponent,
				&e->component);
		if (RET_WAS_ERROR(r))
			return r;
		assert(atom_defined(e->component));

		if (!atom_defined(changes->firstcomponent)) {
			changes->firstcomponent = e->component;
		} else if (changes->firstcomponent != e->component)  {
				fprintf(stderr,
"Warning: %s contains files guessed to be in different components ('%s' vs '%s)!\n",
					filename,
					atoms_components[e->component],
					atoms_components[changes->firstcomponent]);
		}

		if (FE_SOURCE(e->type)) {
			assert (FE_PACKAGE(e->type));
			has_source_package = true;
			if (strcmp(changes->source, e->name) != 0) {
				r = propersourcename(e->name);
				if (RET_WAS_ERROR(r))
					return r;
			}
			if (!atom_defined(changes->srccomponent)) {
				changes->srccomponent = e->component;
			} else if (changes->srccomponent != e->component) {
				fprintf(stderr,
"%s contains source files guessed to be in different components ('%s' vs '%s)!\n",
					filename,
					atoms_components[e->component],
					atoms_components[changes->srccomponent]);
				return RET_ERROR;
			}
		} else if (FE_BINARY(e->type)) {
			r = properpackagename(e->name);
			if (RET_WAS_ERROR(r))
				return r;
			// Let's just check here, perhaps
			if (e->type == fe_UDEB &&
					!atomlist_in(
						&distribution->udebcomponents,
						e->component)) {
				fprintf(stderr,
"Cannot put file '%s' into component '%s', as it is not listed in UDebComponents!\n",
					e->basename,
					atoms_components[e->component]);
				return RET_ERROR;
			}
		} else {
			assert (FE_SOURCE(e->type) || FE_BINARY(e->type));
			fprintf(stderr, "Internal Error!\n");
			return RET_ERROR;
		}
	}

	if (needs_source_package != NULL && !has_source_package) {
		fprintf(stderr,
"'%s' looks like part of an source package, but no dsc file listed in the .changes file!\n",
				needs_source_package->basename);
		return RET_ERROR;
	}
	if (atom_defined(changes->srccomponent)) {
		changes->srcdirectory = calc_sourcedir(changes->srccomponent,
				changes->source);
		if (FAILEDTOALLOC(changes->srcdirectory))
			return RET_ERROR_OOM;
	} else if (distribution->trackingoptions.includechanges ||
			needsourcedir) {
		component_t component = forcecomponent;
		if (!atom_defined(forcecomponent)) {
			for (e = changes->files ; e != NULL ; e = e->next) {
				if (FE_PACKAGE(e->type)){
					component = e->component;
					break;
				}
			}
		}
		if (!atom_defined(component)) {
			fprintf(stderr,
"No component found to place .changes or byhand files in. Aborting.\n");
			return RET_ERROR;
		}
		changes->srcdirectory = calc_sourcedir(component,
				changes->source);
		if (FAILEDTOALLOC(changes->srcdirectory))
			return RET_ERROR_OOM;
	}

	return RET_OK;
}

static inline retvalue checkforarchitecture(const struct fileentry *e, architecture_t architecture) {
	if (!atom_defined(architecture))
		return RET_NOTHING;
	while (e != NULL && e->architecture_into != architecture)
		e = e->next;
	if (e == NULL) {
		if (!IGNORING(unusedarch,
"Architecture header lists architecture '%s', but no files for it!\n",
				atoms_architectures[architecture]))
			return RET_ERROR;
	}
	return RET_OK;
}

static bool can_add_all(const struct atomlist *forcearchitectures, const struct distribution *d) {
	const struct atomlist *da = &d->architectures;
	int i;

	if (forcearchitectures == NULL) {
		return atomlist_hasexcept(da, architecture_source);
	}
	for (i = 0 ; i < forcearchitectures->count ; i++) {
		architecture_t a = forcearchitectures->atoms[i];

		if (a == architecture_source)
			continue;
		if (a == architecture_all)
			return atomlist_hasexcept(da, architecture_source);
		if (atomlist_in(da, a))
			return true;
	}
	return false;
}

static retvalue changes_check(const struct distribution *distribution, const char *filename, struct changes *changes, const struct atomlist *forcearchitectures, const struct atomlist *packagetypes) {
	int i;
	struct fileentry *e;
	retvalue r = RET_OK;
	bool havedsc = false,
	     haveorig = false,
	     havetar = false,
	     havediff = false,
	     havealtsrc = false;

	/* First check for each given architecture, if it has files: */
	if (forcearchitectures != NULL) {
		for (i = 0 ; i < forcearchitectures->count ; i++) {
			architecture_t a = forcearchitectures->atoms[i];

			if (!strlist_in(&changes->architectures,
						atoms_architectures[a])) {
				// TODO: check if this is sensible
				if (!IGNORING(surprisingarch,
"Architecture header does not list the"
" architecture '%s' to be forced in!\n",
						atoms_architectures[a]))
					return RET_ERROR_MISSING;
			}
			r = checkforarchitecture(changes->files, a);
			if (RET_WAS_ERROR(r))
				return r;
		}
	} else {
		bool limitedtosource = false;
		bool limitedtononsource = false;

		if (packagetypes != NULL) {
			limitedtosource = true;
			limitedtononsource = true;
			for (i = 0 ; i < packagetypes->count ; i++) {
				if (packagetypes->atoms[i] == pt_dsc)
					limitedtononsource = false;
				else
					limitedtosource = false;
			}
		}

		for (i = 0 ; i < changes->architectures.count ; i++) {
			const char *architecture = changes->architectures.values[i];
			if (strcmp(architecture, "source") == 0) {
				if (limitedtononsource)
					continue;
			} else {
				if (limitedtosource)
					continue;
			}
			r = checkforarchitecture(changes->files,
					architecture_find(architecture));
			if (RET_WAS_ERROR(r))
				return r;
		}
	}
	/* Then check for each file, if its architecture is sensible
	 * and listed. */
	for (e = changes->files ; e != NULL ; e = e->next) {
		if (e->type == fe_BYHAND || e->type == fe_LOG) {
			/* don't insist on a single architecture for those */
			continue;
		}
		if (atom_defined(e->architecture_into)) {
			if (e->architecture_into == architecture_all) {
				/* "all" can be added if at least one binary
				 *  architecture */
				if (!can_add_all(forcearchitectures,
							distribution))
					e->architecture_into = atom_unknown;
			} else if (!atomlist_in(&distribution->architectures,
						e->architecture_into))
				e->architecture_into = atom_unknown;
		}
		if (!atom_defined(e->architecture_into)) {
			fprintf(stderr,
"Error: '%s' has the wrong architecture to add it to %s!\n",
				e->basename, distribution->codename);
			return RET_ERROR;

		}
		if (!strlist_in(&changes->architectures,
					atoms_architectures[e->architecture_into])) {
			if (!IGNORING(surprisingarch,
"'%s' looks like architecture '%s', but this is not listed in the Architecture-Header!\n",
					e->basename,
					atoms_architectures[e->architecture_into]))
				return RET_ERROR;
		}

		if (e->type == fe_DSC) {
			char *calculatedname;
			if (havedsc) {
				fprintf(stderr,
"I don't know what to do with multiple .dsc files in '%s'!\n", filename);
				return RET_ERROR;
			}
			havedsc = true;
			calculatedname = calc_source_basename(changes->source,
					changes->sourceversion);
			if (FAILEDTOALLOC(calculatedname))
				return RET_ERROR_OOM;
			if (strcmp(calculatedname, e->basename) != 0) {
				fprintf(stderr,
"dsc file name is '%s' instead of the expected '%s'!\n",
					e->basename, calculatedname);
				free(calculatedname);
				return RET_ERROR;
			}
			free(calculatedname);
		} else if (e->type == fe_DIFF) {
			if (havediff) {
				fprintf(stderr,
"I don't know what to do with multiple .diff files in '%s'!\n", filename);
				return RET_ERROR;
			}
			havediff = true;
		} else if (e->type == fe_ORIG) {
			if (haveorig) {
				fprintf(stderr,
"I don't know what to do with multiple .orig.tar.gz files in '%s'!\n", filename);
				return RET_ERROR;
			}
			haveorig = true;
		} else if (e->type == fe_TAR) {
			havetar = true;
		} else if (e->type == fe_ALTSRC) {
			havealtsrc = true;
		}
	}
	if (havetar && !haveorig && havediff) {
		fprintf(stderr,
"I don't know what to do having a .tar.gz not being a .orig.tar.gz and a .diff.gz in '%s'!\n",
				filename);
		return RET_ERROR;
	}
	if (strlist_in(&changes->architectures, "source") && !havedsc &&
			!limitations_missed(forcearchitectures, architecture_source) &&
			!limitations_missed(packagetypes, pt_dsc)) {
		fprintf(stderr,
"I don't know what to do with a source-upload not containing a .dsc in '%s'!\n",
				filename);
		return RET_ERROR;
	}
	if (havedsc && !havediff && !haveorig && !havetar && !havealtsrc) {
		fprintf(stderr,
"I don't know what to do having a .dsc without a .diff.gz or .tar.gz in '%s'!\n",
				filename);
		return RET_ERROR;
	}

	/* check if signatures match files signed: */
	for (e = changes->files ; e != NULL ; e = e->next) {
		size_t el;
		struct fileentry *f;

		if (e->type != fe_SIG) 
			continue;

		el = strlen(e->basename);

		if (el <= 4 || memcmp(e->basename + el - 4, ".asc", 4) != 0)
			continue;

		for (f = changes->files ; f != NULL ; f = f->next) {
			size_t fl = strlen(f->basename);

			if (el != fl + 4)
				continue;
			if (memcmp(e->basename, f->basename, fl) != 0)
				continue;
			break;
		}
		if (f == NULL) {
			fprintf(stderr,
"Signature file without file to be signed: '%s'!\n",
				e->basename);
			return RET_ERROR;
		}
	}

	return r;
}

static retvalue changes_checkfiles(const char *filename, struct changes *changes) {
	struct fileentry *e;
	retvalue r;

	r = RET_NOTHING;

	for (e = changes->files; e != NULL ; e = e->next) {
		//TODO: decide earlier which files to include
		if (e->type == fe_BYHAND) {
			/* byhand files might have the same name and not
			 * contain the version, so store separately */
			assert(changes->srcdirectory!=NULL);
			e->filekey = mprintf("%s/%s_%s_byhand/%s",
					changes->srcdirectory,
					changes->source,
					changes->changesversion,
					e->basename);
		} else if (FE_SOURCE(e->type) || e->type == fe_LOG
				|| e->type == fe_BUILDINFO) {
			assert(changes->srcdirectory!=NULL);
			e->filekey = calc_dirconcat(changes->srcdirectory,
					e->basename);
		} else {
			char *directory;

			// TODO: make this in-situ?
			/* as the directory depends on the sourcename, it can be
			 * different for every file... */
			directory = calc_sourcedir(e->component,
					changes->source);
			if (FAILEDTOALLOC(directory))
				return RET_ERROR_OOM;
			e->filekey = calc_dirconcat(directory, e->basename);
			free(directory);
		}

		if (FAILEDTOALLOC(e->filekey))
			return RET_ERROR_OOM;
		/* do not copy yet, but only check if it could be included */
		r = files_canadd(e->filekey, e->checksums);
		if (RET_WAS_ERROR(r))
			return r;
		/* If is was already there, remember that */
		if (r == RET_NOTHING) {
			e->wasalreadythere = true;
		} else {
		/* and if it needs inclusion check if there is a file */
			char *fullfilename;

			assert(RET_IS_OK(r));
			// TODO: add a --paranoid to also check md5sums before copying?

			fullfilename = calc_dirconcat(
					changes->incomingdirectory,
					e->basename);
			if (FAILEDTOALLOC(fullfilename))
				return RET_ERROR_OOM;
			if (!isregularfile(fullfilename)) {
				fprintf(stderr,
"Cannot find file '%s' needed by '%s'!\n", fullfilename, filename);
				free(fullfilename);
				return RET_ERROR_MISSING;
			}
			free(fullfilename);
		}
	}

	return RET_OK;
}

static retvalue changes_includefiles(struct changes *changes) {
	struct fileentry *e;
	retvalue r;

	r = RET_NOTHING;

	for (e = changes->files; e != NULL ; e = e->next) {
		assert (e->filekey != NULL);

		if (e->wasalreadythere && checksums_iscomplete(e->checksums))
			continue;

		r = files_checkincludefile(changes->incomingdirectory,
				e->basename, e->filekey, &e->checksums);
		if (RET_WAS_ERROR(r))
			return r;
	}

	return r;
}

/* delete the files included */
static retvalue changes_deleteleftoverfiles(struct changes *changes, int delete) {
	struct fileentry *e;
	retvalue result, r;

	if (delete < D_MOVE)
		return RET_OK;

	result = RET_OK;
	// TODO: we currently only see files included here, so D_DELETE
	// only affacts the .changes file.

	for (e = changes->files; e != NULL ; e = e->next) {
		char *fullorigfilename;

		if (delete < D_DELETE && e->filekey == NULL)
			continue;

		fullorigfilename = calc_dirconcat(changes->incomingdirectory,
					e->basename);

		if (unlink(fullorigfilename) != 0) {
			int err = errno;
			fprintf(stderr, "Error deleting '%s': %d=%s\n",
					fullorigfilename, err, strerror(err));
			r = RET_ERRNO(err);
			RET_UPDATE(result, r);
		}
		free(fullorigfilename);
	}

	return result;
}

static retvalue changes_check_sourcefile(struct changes *changes, struct fileentry *dsc,  const char *basefilename, const char *filekey, struct checksums **checksums_p) {
	retvalue r;

	r = files_expect(filekey, *checksums_p, false);
	if (RET_WAS_ERROR(r))
		return r;
	// TODO: get additionals checksum out of database, as future
	// source file completion code might need them...
	if (RET_IS_OK(r))
		return RET_OK;
	if (!IGNORABLE(missingfile)) {
		fprintf(stderr,
"Unable to find %s needed by %s!\n"
"Perhaps you forgot to give dpkg-buildpackage the -sa option,\n"
" or you could try --ignore=missingfile to guess possible files to use.\n",
			filekey, dsc->basename);
		return RET_ERROR_MISSING;
	}
	fprintf(stderr,
"Unable to find %s!\n"
"Perhaps you forgot to give dpkg-buildpackage the -sa option.\n"
"--ignore=missingfile was given, searching for file...\n", filekey);

	return files_checkincludefile(changes->incomingdirectory,
			basefilename, filekey, checksums_p);
}

static retvalue dsc_prepare(struct changes *changes, struct fileentry *dsc,  struct distribution *distribution, const char *dscfilename){
	retvalue r;
	const struct overridedata *oinfo;
	char *dscbasename;
	char *control;
	int i;
	bool broken;

	assert (dsc->section != NULL);
	assert (dsc->priority != NULL);
	assert (atom_defined(changes->srccomponent));
	assert (dsc->basename != NULL);
	assert (dsc->checksums != NULL);
	assert (changes->source != NULL);
	assert (changes->sourceversion != NULL);

	/* First make sure this distribution has a source section at all,
	 * for which it has to be listed in the "Architectures:"-field ;-) */
	if (!atomlist_in(&distribution->architectures, architecture_source)) {
		fprintf(stderr,
"Cannot put a source package into Distribution '%s' not having 'source' in its 'Architectures:'-field!\n",
			distribution->codename);
		/* nota bene: this cannot be forced or ignored, as no target has
		   been created for this. */
		return RET_ERROR;
	}

	/* Then take a closer look in the file: */
	r = sources_readdsc(&dsc->pkg.dsc, dscfilename, dscfilename, &broken);
	if (RET_IS_OK(r) && broken && !IGNORING(brokensignatures,
"'%s' contains only broken signatures.\n"
"This most likely means the file was damaged or edited improperly\n",
				dscfilename))
		r = RET_ERROR;
	if (RET_IS_OK(r))
		r = propersourcename(dsc->pkg.dsc.name);
	if (RET_IS_OK(r))
		r = properversion(dsc->pkg.dsc.version);
	if (RET_IS_OK(r))
		r = properfilenames(&dsc->pkg.dsc.files.names);
	if (RET_WAS_ERROR(r))
		return r;

	if (strcmp(changes->source, dsc->pkg.dsc.name) != 0) {
		/* This cannot be ignored, as too much depends on it yet */
		fprintf(stderr,
"'%s' says it is '%s', while .changes file said it is '%s'\n",
				dsc->basename, dsc->pkg.dsc.name,
				changes->source);
		return RET_ERROR;
	}
	if (strcmp(changes->sourceversion, dsc->pkg.dsc.version) != 0 &&
	    !IGNORING(wrongversion,
"'%s' says it is version '%s', while .changes file said it is '%s'\n",
				dsc->basename, dsc->pkg.dsc.version,
				changes->sourceversion)) {
		return RET_ERROR;
	}

	oinfo = override_search(distribution->overrides.dsc, dsc->pkg.dsc.name);

	free(dsc->pkg.dsc.section);
	dsc->pkg.dsc.section = strdup(dsc->section);
	if (FAILEDTOALLOC(dsc->pkg.dsc.section))
		return RET_ERROR_OOM;
	free(dsc->pkg.dsc.priority);
	dsc->pkg.dsc.priority = strdup(dsc->priority);
	if (FAILEDTOALLOC(dsc->pkg.dsc.priority))
		return RET_ERROR_OOM;

	assert (dsc->pkg.dsc.name != NULL && dsc->pkg.dsc.version != NULL);

	/* Add the dsc file to the list of files in this source package: */
	dscbasename = strdup(dsc->basename);
	if (FAILEDTOALLOC(dscbasename))
		r = RET_ERROR_OOM;
	else
		r = checksumsarray_include(&dsc->pkg.dsc.files,
				dscbasename, dsc->checksums);
	if (RET_WAS_ERROR(r))
		return r;

	/* Calculate the filekeys: */
	r = calc_dirconcats(changes->srcdirectory,
			&dsc->pkg.dsc.files.names, &dsc->needed_filekeys);
	if (RET_WAS_ERROR(r))
		return r;

	/* no one else might have looked yet, if we have them: */

	assert (dsc->pkg.dsc.files.names.count == dsc->needed_filekeys.count);
	for (i = 1 ; i < dsc->pkg.dsc.files.names.count ; i ++) {
		if (!RET_WAS_ERROR(r)) {
			r = changes_check_sourcefile(
				changes, dsc,
				dsc->pkg.dsc.files.names.values[i],
				dsc->needed_filekeys.values[i],
				&dsc->pkg.dsc.files.checksums[i]);
		}
	}

	if (!RET_WAS_ERROR(r))
		r = sources_complete(&dsc->pkg.dsc, changes->srcdirectory,
				oinfo,
				dsc->pkg.dsc.section, dsc->pkg.dsc.priority,
				&control);
	if (RET_IS_OK(r)) {
		free(dsc->pkg.dsc.control);
		dsc->pkg.dsc.control = control;
	}
	return r;
}


static retvalue changes_checkpkgs(struct distribution *distribution, struct changes *changes) {
	struct fileentry *e;
	retvalue r;

	r = RET_NOTHING;

	e = changes->files;
	while (e != NULL) {
		char *fullfilename;
		if (!FE_PACKAGE(e->type)) {
			e = e->next;
			continue;
		}
		fullfilename = files_calcfullfilename(e->filekey);
		if (FAILEDTOALLOC(fullfilename))
			return RET_ERROR_OOM;
		if (e->type == fe_DEB) {
			r = deb_prepare(&e->pkg.deb,
				e->component, e->architecture_into,
				e->section, e->priority,
				pt_deb,
				distribution, fullfilename,
				e->filekey, e->checksums,
				&changes->binaries,
				changes->source, changes->sourceversion);
		} else if (e->type == fe_UDEB) {
			r = deb_prepare(&e->pkg.deb,
				e->component, e->architecture_into,
				e->section, e->priority,
				pt_udeb,
				distribution, fullfilename,
				e->filekey, e->checksums,
				&changes->binaries,
				changes->source, changes->sourceversion);
		} else if (e->type == fe_DDEB) {
			r = deb_prepare(&e->pkg.deb,
				e->component, e->architecture_into,
				e->section, e->priority,
				pt_ddeb,
				distribution, fullfilename,
				e->filekey, e->checksums,
				&changes->binaries,
				changes->source, changes->sourceversion);
		} else if (e->type == fe_DSC) {
			if (!changes->isbinnmu || IGNORING(dscinbinnmu,
"File '%s' looks like a source package, but this .changes looks like a binNMU\n"
"(as '%s' (from Source:) and '%s' (From Version:) differ.)\n",
				e->filekey, changes->sourceversion,
				changes->changesversion)) {

				assert (atom_defined(changes->srccomponent));
				assert (changes->srcdirectory!=NULL);
				r = dsc_prepare(changes, e,
						distribution, fullfilename);
			} else
				r = RET_ERROR;
		}

		free(fullfilename);
		if (RET_WAS_ERROR(r))
			break;
		e = e->next;
	}

	return r;
}

static retvalue changes_includepkgs(struct distribution *distribution, struct changes *changes, /*@null@*/struct trackingdata *trackingdata, const struct atomlist *forcearchitectures, bool *missed_p) {
	struct fileentry *e;
	retvalue result, r;

	*missed_p = false;
	r = distribution_prepareforwriting(distribution);
	if (RET_WAS_ERROR(r))
		return r;

	result = RET_NOTHING;

	e = changes->files;
	while (e != NULL) {
		if (interrupted())
			return RET_ERROR_INTERRUPTED;
		if (e->type == fe_DEB) {
			r = deb_addprepared(e->pkg.deb,
				/* architecture all needs this, the rest is
				 * already filtered out */
				(e->architecture_into == architecture_all)?
					forcearchitectures:NULL,
				pt_deb, distribution, trackingdata);
			if (r == RET_NOTHING)
				*missed_p = true;
		} else if (e->type == fe_DDEB) {
			r = deb_addprepared(e->pkg.deb,
				/* architecture all needs this, the rest is
				 * already filtered out */
				(e->architecture_into == architecture_all)?
					forcearchitectures:NULL,
				pt_ddeb, distribution, trackingdata);
			if (r == RET_NOTHING)
				*missed_p = true;
		} else if (e->type == fe_UDEB) {
			r = deb_addprepared(e->pkg.deb,
				/* architecture all needs this, the rest is
				 * already filtered out */
				(e->architecture_into == architecture_all)?
					forcearchitectures:NULL,
				pt_udeb, distribution, trackingdata);
			if (r == RET_NOTHING)
				*missed_p = true;
		} else if (e->type == fe_DSC) {
			r = dsc_addprepared(&e->pkg.dsc,
					changes->srccomponent,
					&e->needed_filekeys,
					distribution, trackingdata);
			if (r == RET_NOTHING)
				*missed_p = true;
		} else if (e->type == fe_BUILDINFO && trackingdata != NULL) {
			r = trackedpackage_addfilekey(trackingdata->tracks,
					trackingdata->pkg,
					ft_BUILDINFO, e->filekey, false);
			e->filekey = NULL;
		} else if (e->type == fe_LOG && trackingdata != NULL) {
			r = trackedpackage_addfilekey(trackingdata->tracks,
					trackingdata->pkg,
					ft_LOG, e->filekey, false);
			e->filekey = NULL;
		} else if (e->type == fe_BYHAND && trackingdata != NULL) {
			r = trackedpackage_addfilekey(trackingdata->tracks,
					trackingdata->pkg,
					ft_XTRA_DATA, e->filekey, false);
			e->filekey = NULL;
		} else
			r = RET_NOTHING;
		RET_UPDATE(result, r);

		if (RET_WAS_ERROR(r))
			break;
		e = e->next;
	}

	logger_wait();

	return result;
}

static void verifyarchitectures(const struct changes *changes, struct upload_conditions *conditions) {
	const struct fileentry *e;

	for (e = changes->files ; e != NULL ; e = e->next) {
		if (FE_SOURCE(e->type)) {
			if (!uploaders_verifyatom(conditions,
						architecture_source))
				break;
		} else if (FE_BINARY(e->type)) {
			if (!uploaders_verifyatom(conditions,
						e->architecture_into))
				break;
		}
	}
}
static void verifysection(const struct changes *changes, struct upload_conditions *conditions) {
	const struct fileentry *e;

	for (e = changes->files ; e != NULL ; e = e->next) {
		if (FE_SOURCE(e->type)) {
			if (!uploaders_verifystring(conditions, e->section))
				break;
		} else if (FE_BINARY(e->type)) {
			if (!uploaders_verifystring(conditions, e->section))
				break;
		}
	}
}
static void verifybinary(const struct changes *changes, struct upload_conditions *conditions) {
	const struct fileentry *e;

	for (e = changes->files ; e != NULL ; e = e->next) {
		if (FE_BINARY(e->type)) {
			if (!uploaders_verifystring(conditions, e->name))
				break;
		}
	}
}
static void verifybyhands(const struct changes *changes, struct upload_conditions *conditions) {
	const struct fileentry *e;

	for (e = changes->files ; e != NULL ; e = e->next) {
		if (e->type == fe_BYHAND) {
			if (!uploaders_verifystring(conditions, e->name))
				break;
		}
	}
}

static bool permissionssuffice(struct changes *changes, const struct distribution *into, struct upload_conditions *conditions) {
	do switch (uploaders_nextcondition(conditions)) {
		case uc_ACCEPTED:
			return true;
		case uc_REJECTED:
			return false;
		case uc_CODENAME:
			(void)uploaders_verifystring(conditions,
					into->codename);
			break;
		case uc_SOURCENAME:
			assert (changes->source != NULL);
			(void)uploaders_verifystring(conditions,
						changes->source);
			break;
		case uc_SECTIONS:
			verifysection(changes, conditions);
			break;
		case uc_BINARIES:
			verifybinary(changes, conditions);
			break;
		case uc_BYHAND:
			verifybyhands(changes, conditions);
			break;
		case uc_ARCHITECTURES:
			verifyarchitectures(changes, conditions);
			break;
	} while (true);
}

/* insert the given .changes into the mirror in the <distribution>
 * if forcecomponent, forcesection or forcepriority is NULL
 * get it from the files or try to guess it. */
retvalue changes_add(trackingdb const tracks, const struct atomlist *packagetypes, component_t forcecomponent, const struct atomlist *forcearchitectures, const char *forcesection, const char *forcepriority, struct distribution *distribution, const char *changesfilename, int delete) {
	retvalue result, r;
	struct changes *changes;
	struct trackingdata trackingdata;
	bool somethingwasmissed;

	causingfile = changesfilename;

	r = changes_read(changesfilename, &changes,
			packagetypes, forcearchitectures,
			distribution->trackingoptions.includebyhand,
			distribution->trackingoptions.includelogs,
			distribution->trackingoptions.includebuildinfos);
	if (RET_WAS_ERROR(r))
		return r;

	if ((distribution->suite == NULL ||
		!strlist_in(&changes->distributions, distribution->suite)) &&
	    !strlist_in(&changes->distributions, distribution->codename) &&
	    !strlist_intersects(&changes->distributions,
	                       &distribution->alsoaccept)) {
		if (!IGNORING(wrongdistribution,
".changes put in a distribution not listed within it!\n")) {
			changes_free(changes);
			return RET_ERROR;
		}
	}

	/* make sure caller has called distribution_loaduploaders */
	assert (distribution->uploaders == NULL
			|| distribution->uploaderslist != NULL);
	if (distribution->uploaderslist != NULL) {
		struct upload_conditions *conditions;

		r = uploaders_permissions(distribution->uploaderslist,
				changes->signatures, &conditions);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r)) {
			changes_free(changes);
			return r;
		}
		if (!permissionssuffice(changes, distribution, conditions) &&
		    !IGNORING(uploaders,
"No rule allowing this package in found in %s!\n",
			    distribution->uploaders)) {
			free(conditions);
			changes_free(changes);
			return RET_ERROR;
		}
		free(conditions);
	}

	/*look for component, section and priority to be correct or guess them*/
	r = changes_fixfields(distribution, changesfilename, changes,
			forcecomponent, forcesection, forcepriority);

	/* do some tests if values are sensible */
	if (!RET_WAS_ERROR(r))
		r = changes_check(distribution, changesfilename, changes,
				forcearchitectures, packagetypes);

	if (interrupted())
		RET_UPDATE(r, RET_ERROR_INTERRUPTED);

	if (!RET_WAS_ERROR(r))
		r = changes_checkfiles(changesfilename, changes);

	if (interrupted())
		RET_UPDATE(r, RET_ERROR_INTERRUPTED);

	/* add files in the pool */
	if (!RET_WAS_ERROR(r))
		r = changes_includefiles(changes);

	if (!RET_WAS_ERROR(r))
		r = changes_checkpkgs(distribution, changes);

	if (RET_WAS_ERROR(r)) {
		changes_free(changes);
		return r;
	}

	if (tracks != NULL) {
		r = trackingdata_summon(tracks, changes->source,
				changes->sourceversion, &trackingdata);
		if (RET_WAS_ERROR(r)) {
			changes_free(changes);
			return r;
		}
		if (distribution->trackingoptions.includechanges) {
			char *basefilename;
			assert (changes->srcdirectory != NULL);

			basefilename = calc_changes_basename(changes->source,
					changes->changesversion,
					&changes->architectures);
			changes->changesfilekey =
				calc_dirconcat(changes->srcdirectory,
						basefilename);
			free(basefilename);
			if (FAILEDTOALLOC(changes->changesfilekey)) {
				changes_free(changes);
				trackingdata_done(&trackingdata);
				return RET_ERROR_OOM;
			}
			if (interrupted())
				r = RET_ERROR_INTERRUPTED;
			else
				r = files_preinclude(changesfilename,
					changes->changesfilekey,
					NULL);
			if (RET_WAS_ERROR(r)) {
				changes_free(changes);
				trackingdata_done(&trackingdata);
				return r;
			}
		}
	}
	if (interrupted()) {
		if (tracks != NULL)
			trackingdata_done(&trackingdata);
		changes_free(changes);
		return RET_ERROR_INTERRUPTED;
	}

	/* add the source and binary packages in the given distribution */
	result = changes_includepkgs(distribution, changes,
		(tracks!=NULL)?&trackingdata:NULL, forcearchitectures,
		&somethingwasmissed);

	if (RET_WAS_ERROR(result)) {
		if (tracks != NULL) {
			trackingdata_done(&trackingdata);
		}
		changes_free(changes);
		return result;
	}

	if (tracks != NULL) {
		if (changes->changesfilekey != NULL) {
			char *changesfilekey = strdup(changes->changesfilekey);
			assert (changes->srcdirectory != NULL);
			if (FAILEDTOALLOC(changesfilekey)) {
				trackingdata_done(&trackingdata);
				changes_free(changes);
				return RET_ERROR_OOM;
			}

			r = trackedpackage_addfilekey(tracks, trackingdata.pkg,
					ft_CHANGES, changesfilekey, false);
			RET_ENDUPDATE(result, r);
		}
		r = trackingdata_finish(tracks, &trackingdata);
		RET_ENDUPDATE(result, r);
		if (RET_WAS_ERROR(result)) {
			changes_free(changes);
			return result;
		}
	}

	/* if something was included, call --changes notify scripts */
	if (RET_IS_OK(result)) {
		assert (logger_isprepared(distribution->logger));
		logger_logchanges(distribution->logger, distribution->codename,
			changes->source, changes->changesversion,
			changesfilename, changes->changesfilekey);
	}
	/* wait for notify scripts (including those for the packages)
	 * before deleting the .changes */
	logger_wait();

	if ((delete >= D_MOVE && changes->changesfilekey != NULL) ||
			delete >= D_DELETE) {
		if (somethingwasmissed && delete < D_DELETE) {
			if (verbose >= 0) {
				fprintf(stderr,
"Not deleting '%s' as no package was added or some package was missed.\n"
"(Use --delete --delete to delete anyway in such cases)\n",
					changesfilename);
			}
		} else {
			if (verbose >= 5) {
				printf("Deleting '%s'.\n", changesfilename);
			}
			if (unlink(changesfilename) != 0) {
				int e = errno;
				fprintf(stderr, "Error %d deleting '%s': %s\n",
						e, changesfilename,
						strerror(e));
			}
		}
	}
	result = changes_deleteleftoverfiles(changes, delete);
	(void)changes_free(changes);

	return result;
}
