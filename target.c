/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007,2008,2016 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "database.h"
#include "reference.h"
#include "binaries.h"
#include "sources.h"
#include "names.h"
#include "dirs.h"
#include "dpkgversions.h"
#include "tracking.h"
#include "log.h"
#include "files.h"
#include "descriptions.h"
#include "package.h"
#include "target.h"

static char *calc_identifier(const char *codename, component_t component, architecture_t architecture, packagetype_t packagetype) {
	assert (strchr(codename, '|') == NULL);
	assert (codename != NULL); assert (atom_defined(component));
	assert (atom_defined(architecture));
	assert (atom_defined(packagetype));
	if (packagetype == pt_udeb)
		return mprintf("u|%s|%s|%s", codename,
				atoms_components[component],
				atoms_architectures[architecture]);
	else
		return mprintf("%s|%s|%s", codename,
				atoms_components[component],
				atoms_architectures[architecture]);
}


static retvalue target_initialize(/*@dependant@*/struct distribution *distribution, component_t component, architecture_t architecture, packagetype_t packagetype, get_version getversion, get_installdata getinstalldata, get_architecture getarchitecture, get_filekeys getfilekeys, get_checksums getchecksums, get_sourceandversion getsourceandversion, do_reoverride doreoverride, do_retrack doretrack, complete_checksums docomplete, /*@null@*//*@only@*/char *directory, /*@dependent@*/const struct exportmode *exportmode, bool readonly, bool noexport, /*@out@*/struct target **d) {
	struct target *t;

	assert(exportmode != NULL);
	if (FAILEDTOALLOC(directory))
		return RET_ERROR_OOM;

	t = zNEW(struct target);
	if (FAILEDTOALLOC(t)) {
		free(directory);
		return RET_ERROR_OOM;
	}
	t->relativedirectory = directory;
	t->exportmode = exportmode;
	t->distribution = distribution;
	assert (atom_defined(component));
	t->component = component;
	assert (atom_defined(architecture));
	t->architecture = architecture;
	assert (atom_defined(packagetype));
	t->packagetype = packagetype;
	t->identifier = calc_identifier(distribution->codename,
			component, architecture, packagetype);
	if (FAILEDTOALLOC(t->identifier)) {
		(void)target_free(t);
		return RET_ERROR_OOM;
	}
	t->getversion = getversion;
	t->getinstalldata = getinstalldata;
	t->getarchitecture = getarchitecture;
	t->getfilekeys = getfilekeys;
	t->getchecksums = getchecksums;
	t->getsourceandversion = getsourceandversion;
	t->doreoverride = doreoverride;
	t->doretrack = doretrack;
	t->completechecksums = docomplete;
	t->readonly = readonly;
	t->noexport = noexport;
	*d = t;
	return RET_OK;
}

static const char *dist_component_name(component_t component, /*@null@*/const char *fakecomponentprefix) {
	const char *c = atoms_components[component];
	size_t len;

	if (fakecomponentprefix == NULL)
		return c;
	len = strlen(fakecomponentprefix);
	if (strncmp(c, fakecomponentprefix, len) != 0)
		return c;
	if (c[len] != '/')
		return c;
	return c + len + 1;
}

retvalue target_initialize_ubinary(struct distribution *d, component_t component, architecture_t architecture, const struct exportmode *exportmode, bool readonly, bool noexport, const char *fakecomponentprefix, struct target **target) {
	return target_initialize(d, component, architecture, pt_udeb,
			binaries_getversion,
			binaries_getinstalldata,
			binaries_getarchitecture,
			binaries_getfilekeys, binaries_getchecksums,
			binaries_getsourceandversion,
			ubinaries_doreoverride, binaries_retrack,
			binaries_complete_checksums,
			mprintf("%s/debian-installer/binary-%s",
				dist_component_name(component,
					fakecomponentprefix),
				atoms_architectures[architecture]),
			exportmode, readonly, noexport, target);
}
retvalue target_initialize_binary(struct distribution *d, component_t component, architecture_t architecture, const struct exportmode *exportmode, bool readonly, bool noexport, const char *fakecomponentprefix, struct target **target) {
	return target_initialize(d, component, architecture, pt_deb,
			binaries_getversion,
			binaries_getinstalldata,
			binaries_getarchitecture,
			binaries_getfilekeys, binaries_getchecksums,
			binaries_getsourceandversion,
			binaries_doreoverride, binaries_retrack,
			binaries_complete_checksums,
			mprintf("%s/binary-%s",
				dist_component_name(component,
					fakecomponentprefix),
				atoms_architectures[architecture]),
			exportmode, readonly, noexport, target);
}

retvalue target_initialize_source(struct distribution *d, component_t component, const struct exportmode *exportmode, bool readonly, bool noexport, const char *fakecomponentprefix, struct target **target) {
	return target_initialize(d, component, architecture_source, pt_dsc,
			sources_getversion,
			sources_getinstalldata,
			sources_getarchitecture,
			sources_getfilekeys, sources_getchecksums,
			sources_getsourceandversion,
			sources_doreoverride, sources_retrack,
			sources_complete_checksums,
			mprintf("%s/source", dist_component_name(component,
					fakecomponentprefix)),
			exportmode, readonly, noexport, target);
}

retvalue target_free(struct target *target) {
	retvalue result = RET_OK;

	if (target == NULL)
		return RET_OK;
	if (target->packages != NULL) {
		result = target_closepackagesdb(target);
	} else
		result = RET_OK;
	if (target->wasmodified && !target->noexport) {
		fprintf(stderr,
"Warning: database '%s' was modified but no index file was exported.\n"
"Changes will only be visible after the next 'export'!\n",
				target->identifier);
	}

	target->distribution = NULL;
	free(target->identifier);
	free(target->relativedirectory);
	free(target);
	return result;
}

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(struct target *target, bool readonly) {
	retvalue r;

	if (!readonly && target->readonly) {
		fprintf(stderr,
"Error trying to open '%s' read-write in read-only distribution '%s'\n",
				target->identifier,
				target->distribution->codename);
		return RET_ERROR;
	}

	assert (target->packages == NULL);
	if (target->packages != NULL)
		return RET_OK;
	r = database_openpackages(target->identifier, readonly,
			&target->packages);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		target->packages = NULL;
		return r;
	}
	return r;
}

/* this closes databases... */
retvalue target_closepackagesdb(struct target *target) {
	retvalue r;

	if (target->packages == NULL) {
		fprintf(stderr, "Internal Warning: Double close!\n");
		r = RET_OK;
	} else {
		r = table_close(target->packages);
		target->packages = NULL;
	}
	return r;
}

/* Remove a package from the given target. */
retvalue target_removereadpackage(struct target *target, struct logger *logger, const char *name, const char *oldcontrol, struct trackingdata *trackingdata) {
	char *oldpversion = NULL;
	struct strlist files;
	retvalue result, r;
	char *oldsource, *oldsversion;

	assert (target != NULL && target->packages != NULL);
	assert (oldcontrol != NULL && name != NULL);

	if (logger != NULL) {
		/* need to get the version for logging, if not available */
		r = target->getversion(oldcontrol, &oldpversion);
		if (!RET_IS_OK(r))
			oldpversion = NULL;
	}
	r = target->getfilekeys(oldcontrol, &files);
	if (RET_WAS_ERROR(r)) {
		free(oldpversion);
		return r;
	}
	if (trackingdata != NULL) {
		r = target->getsourceandversion(oldcontrol,
				name, &oldsource, &oldsversion);
		if (!RET_IS_OK(r)) {
			oldsource = oldsversion = NULL;
		}
	} else {
		oldsource = oldsversion = NULL;
	}
	if (verbose > 0)
		printf("removing '%s' from '%s'...\n",
				name, target->identifier);
	result = table_deleterecord(target->packages, name, false);
	if (RET_IS_OK(result)) {
		target->wasmodified = true;
		if (oldsource!= NULL && oldsversion != NULL) {
			r = trackingdata_remove(trackingdata,
					oldsource, oldsversion, &files);
			RET_UPDATE(result, r);
		}
		if (trackingdata == NULL)
			target->staletracking = true;
		if (logger != NULL)
			logger_log(logger, target, name,
					NULL, oldpversion,
					NULL, oldcontrol,
					NULL, &files,
					NULL, NULL);
		r = references_delete(target->identifier, &files, NULL);
		RET_UPDATE(result, r);
	}
	strlist_done(&files);
	free(oldpversion);
	return result;
}

/* Remove a package from the given target. */
retvalue target_removepackage(struct target *target, struct logger *logger, const char *name, struct trackingdata *trackingdata) {
	char *oldchunk;
	retvalue r;

	assert(target != NULL && target->packages != NULL && name != NULL);

	r = table_getrecord(target->packages, name, &oldchunk, NULL);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	else if (r == RET_NOTHING) {
		if (verbose >= 10)
			fprintf(stderr, "Could not find '%s' in '%s'...\n",
					name, target->identifier);
		return RET_NOTHING;
	}
	r = target_removereadpackage(target, logger,
			name, oldchunk, trackingdata);
	free(oldchunk);
	return r;
}

/* Like target_removepackage, but delete the package record by cursor */
retvalue package_remove_by_cursor(struct package_cursor *tc, struct logger *logger, struct trackingdata *trackingdata) {
	struct target * const target = tc->target;
	const char * const name = tc->current.name;
	const char * const control = tc->current.control;
	char *oldpversion = NULL;
	struct strlist files;
	retvalue result, r;
	char *oldsource, *oldsversion;

	assert (target != NULL && target->packages != NULL);
	assert (name != NULL && control != NULL);

	if (logger != NULL) {
		/* need to get the version for logging, if not available */
		r = target->getversion(control, &oldpversion);
		if (!RET_IS_OK(r))
			oldpversion = NULL;
	}
	r = target->getfilekeys(control, &files);
	if (RET_WAS_ERROR(r)) {
		free(oldpversion);
		return r;
	}
	if (trackingdata != NULL) {
		r = target->getsourceandversion(control,
				name, &oldsource, &oldsversion);
		if (!RET_IS_OK(r)) {
			oldsource = oldsversion = NULL;
		}
	} else {
		oldsource = oldsversion = NULL;
	}
	if (verbose > 0)
		printf("removing '%s' from '%s'...\n",
				name, target->identifier);
	result = cursor_delete(target->packages, tc->cursor, name, NULL);
	if (RET_IS_OK(result)) {
		target->wasmodified = true;
		if (oldsource != NULL && oldsversion != NULL) {
			r = trackingdata_remove(trackingdata,
					oldsource, oldsversion, &files);
			RET_UPDATE(result, r);
		}
		if (trackingdata == NULL)
			target->staletracking = true;
		if (logger != NULL)
			logger_log(logger, target, name,
					NULL, oldpversion,
					NULL, control,
					NULL, &files,
					NULL, NULL);
		r = references_delete(target->identifier, &files, NULL);
		RET_UPDATE(result, r);
	}
	strlist_done(&files);
	free(oldpversion);
	return result;
}

static retvalue addpackages(struct target *target, const char *packagename, const char *controlchunk, /*@null@*/const char *oldcontrolchunk, const char *version, /*@null@*/const char *oldversion, const struct strlist *files, /*@only@*//*@null@*/struct strlist *oldfiles, /*@null@*/struct logger *logger, /*@null@*/struct trackingdata *trackingdata, architecture_t architecture, /*@null@*//*@only@*/char *oldsource, /*@null@*//*@only@*/char *oldsversion, /*@null@*/const char *causingrule, /*@null@*/const char *suitefrom) {

	retvalue result, r;
	struct table *table = target->packages;
	enum filetype filetype;

	assert (atom_defined(architecture));

	if (architecture == architecture_source)
		filetype = ft_SOURCE;
	else if (architecture == architecture_all)
		filetype = ft_ALL_BINARY;
	else
		filetype = ft_ARCH_BINARY;

	/* mark it as needed by this distribution */

	r = references_insert(target->identifier, files, oldfiles);

	if (RET_WAS_ERROR(r)) {
		if (oldfiles != NULL)
			strlist_done(oldfiles);
		return r;
	}

	/* Add package to the distribution's database */

	if (oldcontrolchunk != NULL) {
		result = table_replacerecord(table, packagename, controlchunk);

	} else {
		result = table_adduniqrecord(table, packagename, controlchunk);
	}

	if (RET_WAS_ERROR(result)) {
		if (oldfiles != NULL)
			strlist_done(oldfiles);
		return result;
	}

	if (logger != NULL)
		logger_log(logger, target, packagename,
				version, oldversion,
				controlchunk, oldcontrolchunk,
				files, oldfiles, causingrule, suitefrom);

	r = trackingdata_insert(trackingdata, filetype, files,
			oldsource, oldsversion, oldfiles);
	RET_UPDATE(result, r);

	/* remove old references to files */

	if (oldfiles != NULL) {
		r = references_delete(target->identifier, oldfiles, files);
		RET_UPDATE(result, r);
		strlist_done(oldfiles);
	}

	return result;
}

retvalue target_addpackage(struct target *target, struct logger *logger, const char *name, const char *version, const char *control, const struct strlist *filekeys, bool downgrade, struct trackingdata *trackingdata, architecture_t architecture, const char *causingrule, const char *suitefrom, struct description *description) {
	struct strlist oldfilekeys, *ofk;
	char *newcontrol;
	char *oldcontrol, *oldsource, *oldsversion;
	char *oldpversion;
	retvalue r;

	assert(target->packages!=NULL);

	r = table_getrecord(target->packages, name, &oldcontrol, NULL);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		ofk = NULL;
		oldsource = NULL;
		oldsversion = NULL;
		oldpversion = NULL;
		oldcontrol = NULL;
	} else {

		r = target->getversion(oldcontrol, &oldpversion);
		if (RET_WAS_ERROR(r) && !IGNORING(brokenold,
"Error parsing old version!\n")) {
			free(oldcontrol);
			return r;
		}
		if (RET_IS_OK(r)) {
			int versioncmp;

			r = dpkgversions_cmp(version, oldpversion, &versioncmp);
			if (RET_WAS_ERROR(r)) {
				if (!IGNORING(brokenversioncmp,
"Parse errors processing versions of %s.\n", name)) {
					free(oldpversion);
					free(oldcontrol);
					return r;
				}
			} else {
				if (versioncmp <= 0) {
					/* new Version is not newer than
					 * old version */
					if (!downgrade) {
						fprintf(stderr,
"Skipping inclusion of '%s' '%s' in '%s', as it has already '%s'.\n",
							name, version,
							target->identifier,
							oldpversion);
						free(oldpversion);
						free(oldcontrol);
						return RET_NOTHING;
					} else if (versioncmp < 0) {
						fprintf(stderr,
"Warning: downgrading '%s' from '%s' to '%s' in '%s'!\n", name,
							oldpversion, version,
							target->identifier);
					} else {
						fprintf(stderr,
"Warning: replacing '%s' version '%s' with equal version '%s' in '%s'!\n", name,
							oldpversion, version,
							target->identifier);
					}
				}
			}
		} else
			oldpversion = NULL;
		r = target->getfilekeys(oldcontrol, &oldfilekeys);
		ofk = &oldfilekeys;
		if (RET_WAS_ERROR(r)) {
			if (IGNORING(brokenold,
"Error parsing files belonging to installed version of %s!\n", name)) {
				ofk = NULL;
				oldsversion = oldsource = NULL;
			} else {
				free(oldcontrol);
				free(oldpversion);
				return r;
			}
		} else if (trackingdata != NULL) {
			r = target->getsourceandversion(oldcontrol,
					name, &oldsource, &oldsversion);
			if (RET_WAS_ERROR(r)) {
				strlist_done(ofk);
				if (IGNORING(brokenold,
"Error searching for source name of installed version of %s!\n", name)) {
					// TODO: free something of oldfilekeys?
					ofk = NULL;
					oldsversion = oldsource = NULL;
				} else {
					free(oldcontrol);
					free(oldpversion);
					return r;
				}
			}
		} else {
			oldsversion = oldsource = NULL;
		}

	}
	newcontrol = NULL;
	r = description_addpackage(target, name, control, oldcontrol,
			description, &newcontrol);
	if (RET_IS_OK(r))
		control = newcontrol;
	if (!RET_WAS_ERROR(r))
		r = addpackages(target, name, control, oldcontrol,
			version, oldpversion,
			filekeys, ofk,
			logger,
			trackingdata, architecture, oldsource, oldsversion,
			causingrule, suitefrom);
	if (RET_IS_OK(r)) {
		target->wasmodified = true;
		if (trackingdata == NULL)
			target->staletracking = true;
	}
	free(newcontrol);
	free(oldpversion);
	free(oldcontrol);

	return r;
}

retvalue target_checkaddpackage(struct target *target, const char *name, const char *version, bool tracking, bool permitnewerold) {
	struct strlist oldfilekeys, *ofk;
	char *oldcontrol, *oldsource, *oldsversion;
	char *oldpversion;
	retvalue r;

	assert(target->packages!=NULL);

	r = table_getrecord(target->packages, name, &oldcontrol, NULL);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		ofk = NULL;
		oldsource = NULL;
		oldsversion = NULL;
		oldpversion = NULL;
		oldcontrol = NULL;
	} else {
		int versioncmp;

		r = target->getversion(oldcontrol, &oldpversion);
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Error extracting version from old '%s' in '%s'. Database corrupted?\n", name, target->identifier);
			free(oldcontrol);
			return r;
		}
		assert (RET_IS_OK(r));

		r = dpkgversions_cmp(version, oldpversion, &versioncmp);
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Parse error comparing version '%s' of '%s' with old version '%s' in '%s'\n.",
					version, name, oldpversion,
					target->identifier);
			free(oldpversion);
			free(oldcontrol);
			return r;
		}
		if (versioncmp <= 0) {
			r = RET_NOTHING;
			if (versioncmp < 0) {
				if (!permitnewerold) {
					fprintf(stderr,
"Error: trying to put version '%s' of '%s' in '%s',\n"
"while there already is the stricly newer '%s' in there.\n"
"(To ignore this error add Permit: older_version.)\n",
						version, name,
						target->identifier,
						oldpversion);
					r = RET_ERROR;
				} else if (verbose >= 0) {
					printf(
"Warning: trying to put version '%s' of '%s' in '%s',\n"
"while there already is '%s' in there.\n",
						version, name,
						target->identifier,
						oldpversion);
				}
			} else if (verbose > 2) {
					printf(
"Will not put '%s' in '%s', as already there with same version '%s'.\n",
						name, target->identifier,
						oldpversion);

			}
			free(oldpversion);
			free(oldcontrol);
			return r;
		}
		r = target->getfilekeys(oldcontrol, &oldfilekeys);
		ofk = &oldfilekeys;
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Error extracting installed files from old '%s' in '%s'.\nDatabase corrupted?\n",
				name, target->identifier);
			free(oldcontrol);
			free(oldpversion);
			return r;
		}
		if (tracking) {
			r = target->getsourceandversion(oldcontrol,
					name, &oldsource, &oldsversion);
			if (RET_WAS_ERROR(r)) {
				fprintf(stderr,
"Error extracting source name and version from '%s' in '%s'. Database corrupted?\n",
						name, target->identifier);
				strlist_done(ofk);
				free(oldcontrol);
				free(oldpversion);
				return r;
			}
			/* TODO: check if tracking would succeed */
			free(oldsversion);
			free(oldsource);
		}
		strlist_done(ofk);
	}
	free(oldpversion);
	free(oldcontrol);
	return RET_OK;
}

retvalue target_rereference(struct target *target) {
	retvalue result, r;
	struct package_cursor iterator;

	if (verbose > 1) {
		if (verbose > 2)
			printf("Unlocking dependencies of %s...\n",
					target->identifier);
		else
			printf("Rereferencing %s...\n",
					target->identifier);
	}

	result = references_remove(target->identifier);
	if (verbose > 2)
		printf("Referencing %s...\n", target->identifier);

	r = package_openiterator(target, READONLY, &iterator);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	while (package_next(&iterator)) {
		struct strlist filekeys;

		r = target->getfilekeys(iterator.current.control, &filekeys);
		RET_UPDATE(result, r);
		if (!RET_IS_OK(r))
			continue;
		if (verbose > 10) {
			fprintf(stderr, "adding references to '%s' for '%s': ",
					target->identifier,
					iterator.current.name);
			(void)strlist_fprint(stderr, &filekeys);
			(void)putc('\n', stderr);
		}
		r = references_insert(target->identifier, &filekeys, NULL);
		strlist_done(&filekeys);
		RET_UPDATE(result, r);
	}
	r = package_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue package_referenceforsnapshot(struct package *package, void *data) {
	const char *identifier = data;
	struct strlist filekeys;
	retvalue r;

	r = package->target->getfilekeys(package->control, &filekeys);
	if (RET_WAS_ERROR(r))
		return r;
	if (verbose > 15) {
		fprintf(stderr, "adding references to '%s' for '%s': ",
				identifier, package->name);
		(void)strlist_fprint(stderr, &filekeys);
		(void)putc('\n', stderr);
	}
	r = references_add(identifier, &filekeys);
	strlist_done(&filekeys);
	return r;
}

retvalue package_check(struct package *package, UNUSED(void *pd)) {
	struct target *target = package->target;
	struct checksumsarray files;
	struct strlist expectedfilekeys;
	char *dummy, *version;
	retvalue result = RET_OK, r;
	architecture_t package_architecture;

	r = target->getversion(package->control, &version);
	if (!RET_IS_OK(r)) {
		fprintf(stderr,
"Error extraction version number from package control info of '%s'!\n",
				package->name);
		if (r == RET_NOTHING)
			r = RET_ERROR_MISSING;
		return r;
	}
	r = target->getarchitecture(package->control, &package_architecture);
	if (!RET_IS_OK(r)) {
		fprintf(stderr,
"Error extraction architecture from package control info of '%s'!\n",
				package->name);
		if (r == RET_NOTHING)
			r = RET_ERROR_MISSING;
		return r;
	}
	/* check if the architecture matches the architecture where this
	 * package belongs to. */
	if (target->architecture != package_architecture &&
	    package_architecture != architecture_all) {
		fprintf(stderr,
"Wrong architecture '%s' of package '%s' in '%s'!\n",
				atoms_architectures[package_architecture],
				package->name, target->identifier);
		result = RET_ERROR;
	}
	r = target->getinstalldata(target, package->name, version,
			package_architecture, package->control, &dummy,
			&expectedfilekeys, &files);
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr,
"Error extracting information of package '%s'!\n",
				package->name);
		result = r;
	}
	free(version);
	if (RET_IS_OK(r)) {
		free(dummy);
		if (!strlist_subset(&expectedfilekeys, &files.names, NULL) ||
		    !strlist_subset(&expectedfilekeys, &files.names, NULL)) {
			(void)fprintf(stderr,
"Reparsing the package information of '%s' yields to the expectation to find:\n",
					package->name);
			(void)strlist_fprint(stderr, &expectedfilekeys);
			(void)fputs("but found:\n", stderr);
			(void)strlist_fprint(stderr, &files.names);
			(void)putc('\n', stderr);
			result = RET_ERROR;
		}
		strlist_done(&expectedfilekeys);
	} else {
		r = target->getchecksums(package->control, &files);
		if (r == RET_NOTHING)
			r = RET_ERROR;
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Even more errors extracting information of package '%s'!\n",
					package->name);
			return r;
		}
	}

	if (verbose > 10) {
		fprintf(stderr, "checking files of '%s'\n", package->name);
	}
	r = files_expectfiles(&files.names, files.checksums);
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr, "Files are missing for '%s'!\n", package->name);
	}
	RET_UPDATE(result, r);
	if (verbose > 10) {
		(void)fprintf(stderr, "checking references to '%s' for '%s': ",
				target->identifier, package->name);
		(void)strlist_fprint(stderr, &files.names);
		(void)putc('\n', stderr);
	}
	r = references_check(target->identifier, &files.names);
	RET_UPDATE(result, r);
	checksumsarray_done(&files);
	return result;
}

/* Reapply override information */

retvalue target_reoverride(struct target *target, struct distribution *distribution) {
	struct package_cursor iterator;
	retvalue result, r;

	assert(target->packages == NULL);
	assert(distribution != NULL);

	if (verbose > 1) {
		fprintf(stderr,
"Reapplying overrides packages in '%s'...\n",
				target->identifier);
	}

	r = package_openiterator(target, READWRITE, &iterator);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (package_next(&iterator)) {
		char *newcontrolchunk = NULL;

		r = target->doreoverride(target, iterator.current.name,
				iterator.current.control,
				&newcontrolchunk);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r)) {
			if (verbose > 0)
				(void)fputs(
"target_reoverride: Stopping procession of further packages due to previous errors\n",
						stderr);
			break;
		}
		if (RET_IS_OK(r)) {
			r = package_newcontrol_by_cursor(&iterator,
				newcontrolchunk, strlen(newcontrolchunk));
			free(newcontrolchunk);
			if (RET_WAS_ERROR(r)) {
				result = r;
				break;
			}
			target->wasmodified = true;
		}
	}
	r = package_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

/* Readd checksum information */

static retvalue complete_package_checksums(struct target *target, const char *control, char **n) {
	struct checksumsarray files;
	retvalue r;

	r = target->getchecksums(control, &files);
	if (!RET_IS_OK(r))
		return r;

	r = files_checkorimprove(&files.names, files.checksums);
	if (!RET_IS_OK(r)) {
		checksumsarray_done(&files);
		return r;
	}
	r = target->completechecksums(control,
			&files.names, files.checksums, n);
	checksumsarray_done(&files);
	return r;
}

retvalue target_redochecksums(struct target *target, struct distribution *distribution) {
	struct package_cursor iterator;
	retvalue result, r;

	assert(target->packages == NULL);
	assert(distribution != NULL);

	if (verbose > 1) {
		fprintf(stderr,
"Redoing checksum information for packages in '%s'...\n",
				target->identifier);
	}

	r = package_openiterator(target, READWRITE, &iterator);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (package_next(&iterator)) {
		char *newcontrolchunk = NULL;

		r = complete_package_checksums(target, iterator.current.control,
				&newcontrolchunk);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		if (RET_IS_OK(r)) {
			r = package_newcontrol_by_cursor(&iterator,
				newcontrolchunk, strlen(newcontrolchunk));
			free(newcontrolchunk);
			if (RET_WAS_ERROR(r)) {
				result = r;
				break;
			}
			target->wasmodified = true;
		}
	}
	r = package_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

/* export a database */

retvalue target_export(struct target *target, bool onlyneeded, bool snapshot, struct release *release) {
	retvalue result;
	bool onlymissing;

	assert (!target->noexport);

	if (verbose > 5) {
		if (onlyneeded)
			printf(" looking for changes in '%s'...\n",
					target->identifier);
		else
			printf(" exporting '%s'...\n", target->identifier);
	}

	/* not exporting if file is already there? */
	onlymissing = onlyneeded && !target->wasmodified;

	result = export_target(target->relativedirectory, target,
			target->exportmode, release, onlymissing, snapshot);

	if (!RET_WAS_ERROR(result) && !snapshot) {
		target->saved_wasmodified =
			target->saved_wasmodified || target->wasmodified;
		target->wasmodified = false;
	}
	return result;
}

retvalue package_rerunnotifiers(struct package *package, UNUSED(void *data)) {
	struct target *target = package->target;
	struct logger *logger = target->distribution->logger;
	struct strlist filekeys;
	char *version;
	retvalue r;

	r = target->getversion(package->control, &version);
	if (!RET_IS_OK(r)) {
		fprintf(stderr,
"Error extraction version number from package control info of '%s'!\n",
				package->name);
		if (r == RET_NOTHING)
			r = RET_ERROR_MISSING;
		return r;
	}
	r = target->getfilekeys(package->control, &filekeys);
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr,
"Error extracting information about used files from package '%s'!\n",
				package->name);
		free(version);
		return r;
	}
	r = logger_reruninfo(logger, target, package->name, version,
			package->control, &filekeys);
	strlist_done(&filekeys);
	free(version);
	return r;
}

retvalue package_get(struct target *target, const char *name, const char *version, struct package *pkg) {
	retvalue result, r;
	bool database_closed;

	assert (version == NULL); /* not yet implemented */

	memset(pkg, 0, sizeof(*pkg));

	database_closed = target->packages == NULL;

	if (database_closed) {
		r = target_initpackagesdb(target, READONLY);
		if (RET_WAS_ERROR(r))
			return r;
	}
	result = table_getrecord(target->packages, name,
			&pkg->pkgchunk, &pkg->controllen);
	if (RET_IS_OK(result)) {
		pkg->target = target;
		pkg->name = name;
		pkg->control = pkg->pkgchunk;
	}
	if (database_closed) {
		r = target_closepackagesdb(target);
		if (RET_WAS_ERROR(r)) {
			package_done(pkg);
			return r;
		}
	}
	return result;
}

retvalue package_openiterator(struct target *t, bool readonly, /*@out@*/struct package_cursor *tc) {
	retvalue r, r2;
	struct cursor *c;

	r = target_initpackagesdb(t, readonly);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	r = table_newglobalcursor(t->packages, &c);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		r2 = target_closepackagesdb(t);
		RET_UPDATE(r, r2);
		return r;
	}
	tc->target = t;
	tc->cursor = c;
	memset(&tc->current, 0, sizeof(tc->current));
	return RET_OK;
}

bool package_next(struct package_cursor *tc) {
	bool success;
	package_done(&tc->current);
	success = cursor_nexttempdata(tc->target->packages, tc->cursor,
			&tc->current.name, &tc->current.control,
			&tc->current.controllen);
	if (!success)
		memset(&tc->current, 0, sizeof(tc->current));
	else
		tc->current.target = tc->target;
	return success;
}

retvalue package_closeiterator(struct package_cursor *tc) {
	retvalue result, r;

	package_done(&tc->current);
	result = cursor_close(tc->target->packages, tc->cursor);
	r = target_closepackagesdb(tc->target);
	RET_UPDATE(result, r);
	return result;
}
