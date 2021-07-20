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
	else if (packagetype == pt_ddeb)
		return mprintf("d|%s|%s|%s", codename,
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
retvalue target_initialize_dbinary(struct distribution *d, component_t component, architecture_t architecture, const struct exportmode *exportmode, bool readonly, bool noexport, const char *fakecomponentprefix, struct target **target) {
	return target_initialize(d, component, architecture, pt_ddeb,
			binaries_getversion,
			binaries_getinstalldata,
			binaries_getarchitecture,
			binaries_getfilekeys, binaries_getchecksums,
			binaries_getsourceandversion,
			/* we use the main overrides */
			binaries_doreoverride, binaries_retrack,
			binaries_complete_checksums,
			/* FIXME: we don't know what the Debian archive layout
			 * is going to look like yet, so take a guess based
			 * on udebs */
			mprintf("%s/debug/binary-%s",
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
retvalue package_remove(struct package *old, struct logger *logger, struct trackingdata *trackingdata) {
	struct strlist files;
	retvalue result, r;
	char *key;

	assert (old->target != NULL && old->target->packages != NULL);

	(void)package_getversion(old);
	if (verbose >= 15)
		fprintf(stderr, "trace: package_remove(old.name=%s, old.version=%s, old.target.identifier=%s) called.\n",
		        old->name, old->version, old->target->identifier);
	r = old->target->getfilekeys(old->control, &files);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	if (trackingdata != NULL) {
		(void)package_getsource(old);
	}
	if (verbose > 0)
		printf("removing '%s=%s' from '%s'...\n",
				old->name, old->version, old->target->identifier);
	key = package_primarykey(old->name, old->version);
	result = table_deleterecord(old->target->packages, key, false);
	free(key);
	if (RET_IS_OK(result)) {
		old->target->wasmodified = true;
		if (trackingdata != NULL && old->source != NULL
				&& old->sourceversion != NULL) {
			r = trackingdata_remove(trackingdata,
					old->source, old->sourceversion, &files);
			RET_UPDATE(result, r);
		}
		if (trackingdata == NULL)
			old->target->staletracking = true;
		if (logger != NULL)
			logger_log(logger, old->target, old->name,
					NULL,
					(old->version == NULL)
						? "#unparseable#"
						: old->version,
					NULL, &files,
					NULL, NULL);
		r = references_delete(old->target->identifier, &files, NULL);
		RET_UPDATE(result, r);
	}
	strlist_done(&files);
	return result;
}

/* Remove a package from the given target. */
retvalue target_removepackage(struct target *target, struct logger *logger, const char *name, const char *version, struct trackingdata *trackingdata) {
	struct package old;
	retvalue r;

	assert(target != NULL && target->packages != NULL && name != NULL);
	if (verbose >= 15)
		fprintf(stderr, "trace: target_removepackage(target.identifier=%s, name=%s, version=%s) called.\n",
		        target->identifier, name, version);

	r = package_get(target, name, version, &old);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	else if (r == RET_NOTHING) {
		if (verbose >= 10) {
			if (version == NULL)
				fprintf(stderr, "Could not find '%s' in '%s'...\n",
						name, target->identifier);
			else
				fprintf(stderr, "Could not find '%s=%s' in '%s'...\n",
						name, version, target->identifier);
		}
		return RET_NOTHING;
	}
	r = package_remove(&old, logger, trackingdata);
	package_done(&old);
	return r;
}

/* Like target_removepackage, but delete the package record by cursor */
retvalue package_remove_by_cursor(struct package_cursor *tc, struct logger *logger, struct trackingdata *trackingdata) {
	struct target * const target = tc->target;
	struct package *old = &tc->current;
	struct strlist files;
	retvalue result, r;

	assert (target != NULL && target->packages != NULL);
	assert (target == old->target);

	if (logger != NULL || verbose > 0) {
		(void)package_getversion(old);
	}
	r = old->target->getfilekeys(old->control, &files);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	if (trackingdata != NULL) {
		(void)package_getsource(old);
	}
	if (verbose > 0)
		printf("removing '%s=%s' from '%s'...\n",
				old->name, old->version, old->target->identifier);
	result = cursor_delete(target->packages, tc->cursor, old->name, old->version);
	if (RET_IS_OK(result)) {
		old->target->wasmodified = true;
		if (trackingdata != NULL && old->source != NULL
				&& old->sourceversion != NULL) {
			r = trackingdata_remove(trackingdata,
					old->source, old->sourceversion, &files);
			RET_UPDATE(result, r);
		}
		if (trackingdata == NULL)
			old->target->staletracking = true;
		if (logger != NULL)
			logger_log(logger, old->target, old->name,
					NULL,
					(old->version == NULL)
						? "#unparseable"
						: old->version,
					NULL, &files,
					NULL, NULL);
		r = references_delete(old->target->identifier, &files, NULL);
		RET_UPDATE(result, r);
	}
	strlist_done(&files);
	return result;
}

static retvalue archive_package(struct target *target, const struct package *package, const struct strlist *files, /*@null@*/const char *causingrule, /*@null@*/const char *suitefrom) {
	struct strlist filekeys;
	struct target *archive_target;
	struct trackingdata trackingdata;
	trackingdb tracks = NULL;
	bool close_database, close_trackingdb = false;
	retvalue result, r;

	if (verbose >= 15)
		fprintf(stderr, "trace: archive_package(target.identifier=%s, package->name=%s, package->version=%s) called.\n",
		        target->identifier, package->name, package->version);

	if (target->distribution->archive != NULL) {
		archive_target = distribution_gettarget(target->distribution->archive, target->component,
		                                        target->architecture, target->packagetype);
		if (archive_target == NULL) {
			fprintf(stderr,
"Warning: Cannot archive '%s=%s' from '%s' to '%s' since '%s' has no matching component/architecture/packagetype.\n",
			        package->name, package->version, target->distribution->codename,
			        target->distribution->archive->codename,
			        target->distribution->archive->codename);
		} else {
			close_database = archive_target->packages == NULL;
			if (close_database) {
				result = target_initpackagesdb(archive_target, READWRITE);
				if (RET_WAS_ERROR(result)) {
					return result;
				}
			}
			if (files == NULL) {
				result = archive_target->getfilekeys(package->control, &filekeys);
				if (RET_WAS_ERROR(result))
					return result;
				files = &filekeys;
			}
			if (archive_target->distribution->tracking != dt_NONE) {
				close_trackingdb = archive_target->distribution->trackingdb == NULL;
				if (close_trackingdb) {
					r = tracking_initialize(&tracks, archive_target->distribution, false);
					if (RET_WAS_ERROR(r))
						return r;
				} else {
					tracks = archive_target->distribution->trackingdb;
				}
				r = trackingdata_summon(tracks, package->source, package->version, &trackingdata);
				if (RET_WAS_ERROR(r))
					return r;
			}
			// TODO: Check whether this is the best place to set 'selected'
			target->distribution->archive->selected = true;
			result = distribution_prepareforwriting(archive_target->distribution);
			if (!RET_WAS_ERROR(result)) {
				result = target_addpackage(archive_target, target->distribution->archive->logger,
					              package->name, package->version, package->control,
					              files, false, (tracks != NULL) ? &trackingdata : NULL,
					              target->architecture, causingrule, suitefrom);
				RET_UPDATE(target->distribution->archive->status, result);
			}
			if (close_database) {
				r = target_closepackagesdb(archive_target);
				RET_UPDATE(result, r);
			}
			if (tracks != NULL) {
				r = trackingdata_finish(tracks, &trackingdata);
				RET_UPDATE(result, r);
				if (close_trackingdb) {
					r = tracking_done(tracks, archive_target->distribution);
					RET_UPDATE(result, r);
				}
			}
			if (RET_WAS_ERROR(result)) {
				return result;
			}
		}
	}
	return RET_OK;
}

static retvalue addpackages(struct target *target, const char *packagename, const char *controlchunk, const char *version, const struct strlist *files, /*@null@*/const struct package *old, /*@null@*/const struct strlist *oldfiles, /*@null@*/struct logger *logger, /*@null@*/struct trackingdata *trackingdata, architecture_t architecture, /*@null@*/const char *causingrule, /*@null@*/const char *suitefrom) {

	retvalue result = RET_OK, r;
	char *key;
	struct table *table = target->packages;
	enum filetype filetype;

	if (verbose >= 15)
		fprintf(stderr, "trace: addpackages(target.identifier=%s, packagename=%s, version=%s, old->version=%s) called.\n",
		        target->identifier, packagename, version, old != NULL ? old->version : NULL);
	assert (atom_defined(architecture));

	if (architecture == architecture_source)
		filetype = ft_SOURCE;
	else if (architecture == architecture_all)
		filetype = ft_ALL_BINARY;
	else
		filetype = ft_ARCH_BINARY;

	/* mark it as needed by this distribution */

	r = references_insert(target->identifier, files, oldfiles);

	if (RET_WAS_ERROR(r))
		return r;

	/* Add package to the distribution's database */

	if (old != NULL && old->control != NULL) {
		key = package_primarykey(old->name, old->version);
		r = archive_package(target, old, oldfiles, causingrule, suitefrom);
		RET_UPDATE(result, r);
		if (RET_IS_OK(r)) {
			r = table_deleterecord(table, key, false);
			RET_UPDATE(result, r);
		}
		free(key);
	}

	key = package_primarykey(packagename, version);
	r = table_adduniqrecord(table, key, controlchunk);
	free(key);

	if (RET_WAS_ERROR(r))
		return r;

	if (logger != NULL) {
		logger_log(logger, target, packagename,
				version,
				/* the old version, NULL if there is
				 * no old package,
				 * "#unparseable" if there is old but
				 * no version available */
				(old==NULL)
				? NULL
				: (old->version == NULL)
				  ? "#unparseable"
				  : old->version,
				files, oldfiles, causingrule, suitefrom);
	}

	if (trackingdata != NULL) {
		r = trackingdata_insert(trackingdata,
			filetype, files, old, oldfiles);
		RET_UPDATE(result, r);
	}

	/* remove old references to files */

	if (oldfiles != NULL) {
		r = references_delete(target->identifier, oldfiles, files);
		RET_UPDATE(result, r);
	}

	return result;
}

retvalue target_addpackage(struct target *target, struct logger *logger, const char *name, const char *version, const char *control, const struct strlist *filekeys, bool downgrade, struct trackingdata *trackingdata, architecture_t architecture, const char *causingrule, const char *suitefrom) {
	struct strlist oldfilekeys, *ofk = NULL;
	char *newcontrol;
	struct package_cursor iterator = {NULL};
	struct package old;
	retvalue r;

	if (verbose >= 15)
		fprintf(stderr, "trace: target_addpackage(target.identifier=%s, name=%s, version=%s) called.\n",
		        target->identifier, name, version);
	assert(target->packages!=NULL);

	r = package_get(target, name, version, &old);
	if (RET_WAS_ERROR(r)) {
		package_done(&old);
		return r;
	} else if (RET_IS_OK(r)) {
		if (!downgrade) {
			fprintf(stderr, "Skipping inclusion of '%s' '%s' in '%s', as this version already exists.\n",
					name, version, target->identifier);
			package_done(&old);
			return RET_NOTHING;
		} else {
			r = package_getversion(&old);
			if (RET_WAS_ERROR(r) && !IGNORING(brokenold, "Error parsing old version!\n")) {
				package_done(&old);
				return r;
			}
			fprintf(stderr, "Warning: replacing '%s' version '%s' with equal version '%s' in '%s'!\n",
			        name, old.version, version, target->identifier);
		}
	} else if (target->distribution->limit > 0) {
		package_done(&old);
		r = package_openduplicateiterator(target, name, target->distribution->limit - 1, &iterator);
		if (RET_WAS_ERROR(r)) {
			return r;
		}
		if (RET_IS_OK(r)) {
			r = package_getversion(&iterator.current);
			if (RET_WAS_ERROR(r) && !IGNORING(brokenold, "Error parsing old version!\n")) {
				retvalue r2 = package_closeiterator(&iterator);
				RET_ENDUPDATE(r, r2);
				return r;
			}
			if (RET_IS_OK(r)) {
				int versioncmp;

				r = dpkgversions_cmp(version, iterator.current.version, &versioncmp);
				if (RET_WAS_ERROR(r)) {
					if (!IGNORING(brokenversioncmp, "Parse errors processing versions of %s.\n", name)) {
						retvalue r2 = package_closeiterator(&iterator);
						RET_ENDUPDATE(r, r2);
						return r;
					}
				} else if (versioncmp < 0) {
					// new Version is older than the old version that will be replaced
					if (!downgrade) {
						fprintf(stderr,
"Skipping inclusion of '%s' '%s' in '%s', as it has already '%s'.\n",
							name, version,
							target->identifier,
							iterator.current.version);
						package_done(&old);
						return RET_NOTHING;
					} else {
						fprintf(stderr,
"Warning: downgrading '%s' from '%s' to '%s' in '%s'!\n", name,
							iterator.current.version,
							version,
							target->identifier);
					}
				}
				old.target = target;
				old.name = iterator.current.name;
				old.control = iterator.current.control;
				old.controllen = iterator.current.controllen;
				old.version = iterator.current.version;
			}
		}
	} else {
		// Keep all package versions in the archive.
		package_done(&old);
	}

	if (old.name != NULL) {
		r = target->getfilekeys(old.control, &oldfilekeys);
		ofk = &oldfilekeys;
		if (RET_WAS_ERROR(r)) {
			if (IGNORING(brokenold,
"Error parsing files belonging to installed version of %s!\n", name)) {
				ofk = NULL;
			} else {
				package_done(&old);
				if (iterator.cursor != NULL) {
					retvalue r2 = package_closeiterator(&iterator);
					RET_ENDUPDATE(r, r2);
				}
				return r;
			}
		} else if (trackingdata != NULL) {
			r = package_getsource(&old);
			if (RET_WAS_ERROR(r)) {
				strlist_done(ofk);
				if (IGNORING(brokenold,
"Error searching for source name of installed version of %s!\n", name)) {
					// TODO: free something of oldfilekeys?
					ofk = NULL;
				} else {
					package_done(&old);
					if (iterator.cursor != NULL) {
						retvalue r2 = package_closeiterator(&iterator);
						RET_ENDUPDATE(r, r2);
					}
					return r;
				}
			}
		}
	}

	newcontrol = NULL;
	r = description_addpackage(target, name, control, &newcontrol);
	if (RET_IS_OK(r))
		control = newcontrol;
	if (!RET_WAS_ERROR(r))
		r = addpackages(target, name, control,
			version,
			filekeys,
			&old, ofk,
			logger,
			trackingdata, architecture,
			causingrule, suitefrom);
	if (ofk != NULL)
		strlist_done(ofk);
	if (RET_IS_OK(r)) {
		target->wasmodified = true;
		if (trackingdata == NULL)
			target->staletracking = true;
	}
	free(newcontrol);
	package_done(&old);

	if (iterator.cursor != NULL) {
		// Remove all older versions (that exceed the current limit)
		retvalue r2;
		while(package_next(&iterator)) {
			r2 = package_getversion(&iterator.current);
			RET_UPDATE(r, r2);
			if (RET_WAS_ERROR(r2))
				continue;
			if (strcmp(version, iterator.current.version) == 0) {
				// Do not archive/remove the newly added package!
				continue;
			}
			r2 = package_getsource(&iterator.current);
			if (RET_WAS_ERROR(r2))
				continue;
			r2 = archive_package(target, &iterator.current, NULL, causingrule, suitefrom);
			RET_UPDATE(r, r2);
			if (RET_WAS_ERROR(r2))
				continue;
			r2 = package_remove_by_cursor(&iterator, logger, trackingdata);
			RET_UPDATE(r, r2);
		}
		r2 = package_closeiterator(&iterator);
		RET_ENDUPDATE(r, r2);
	}
	return r;
}

retvalue target_checkaddpackage(struct target *target, const char *name, const char *version, bool tracking, bool permitnewerold) {
	struct strlist oldfilekeys, *ofk;
	struct package old;
	retvalue r;

	assert(target->packages!=NULL);

	r = package_get(target, name, NULL, &old);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		ofk = NULL;
	} else {
		int versioncmp;

		r = package_getversion(&old);
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Error extracting version from old '%s' in '%s'. Database corrupted?\n", name, target->identifier);
			package_done(&old);
			return r;
		}
		assert (RET_IS_OK(r));

		r = dpkgversions_cmp(version, old.version, &versioncmp);
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Parse error comparing version '%s' of '%s' with old version '%s' in '%s'\n.",
					version, name, old.version,
					target->identifier);
			package_done(&old);
			return r;
		}
		if (versioncmp < 0) {
			if (!permitnewerold) {
				fprintf(stderr,
"Error: trying to put version '%s' of '%s' in '%s',\n"
"while there already is the stricly newer '%s' in there.\n"
"(To ignore this error add Permit: older_version.)\n",
					version, name,
					target->identifier,
					old.version);
				package_done(&old);
				return RET_ERROR;
			} else if (verbose > 2) {
				printf("Puting version '%s' of '%s' in '%s', while there already is '%s' in there.\n",
					version, name, target->identifier, old.version);
			}
		} else if (versioncmp == 0) {
			if (verbose > 2) {
				printf(
"Will not put '%s' in '%s', as already there with same version '%s'.\n",
					name, target->identifier,
					old.version);
			}
			package_done(&old);
			return RET_NOTHING;
		}
		r = target->getfilekeys(old.control, &oldfilekeys);
		ofk = &oldfilekeys;
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Error extracting installed files from old '%s' in '%s'.\nDatabase corrupted?\n",
				name, target->identifier);
			package_done(&old);
			return r;
		}
		if (tracking) {
			r = package_getsource(&old);
			if (RET_WAS_ERROR(r)) {
				fprintf(stderr,
"Error extracting source name and version from '%s' in '%s'. Database corrupted?\n",
						name, target->identifier);
				strlist_done(ofk);
				package_done(&old);
				return r;
			}
			/* TODO: check if tracking would succeed */
		}
		strlist_done(ofk);
		package_done(&old);
	}
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

	r = package_openiterator(target, READONLY, true, &iterator);
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
	retvalue result = RET_OK, r;

	r = package_getversion(package);
	if (!RET_IS_OK(r)) {
		fprintf(stderr,
"Error extraction version number from package control info of '%s'!\n",
				package->name);
		if (r == RET_NOTHING)
			r = RET_ERROR_MISSING;
		return r;
	}
	r = package_getarchitecture(package);
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
	if (target->architecture != package->architecture &&
	    package->architecture != architecture_all) {
		fprintf(stderr,
"Wrong architecture '%s' of package '%s' in '%s'!\n",
				atoms_architectures[package->architecture],
				package->name, target->identifier);
		result = RET_ERROR;
	}
	r = target->getchecksums(package->control, &files);
	if (r == RET_NOTHING)
		r = RET_ERROR;
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr,
"Error extracting information of package '%s'!\n",
				package->name);
		return r;
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

	r = package_openiterator(target, READWRITE, true, &iterator);
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

	r = package_openiterator(target, READWRITE, true, &iterator);
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
	retvalue r;

	r = package_getversion(package);
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
		return r;
	}
	r = logger_reruninfo(logger, target, package->name, package->version,
			&filekeys);
	strlist_done(&filekeys);
	return r;
}

retvalue package_get(struct target *target, const char *name, const char *version, struct package *pkg) {
	retvalue result, r;
	bool database_closed;

	if (verbose >= 15)
		fprintf(stderr, "trace: package_get(target.identifier=%s, packagename=%s, version=%s) called.\n",
		        target->identifier, name, version);

	memset(pkg, 0, sizeof(*pkg));

	database_closed = target->packages == NULL;

	if (database_closed) {
		r = target_initpackagesdb(target, READONLY);
		if (RET_WAS_ERROR(r))
			return r;
	}

	if (version == NULL) {
		result = table_getrecord(target->packages, true, name,
				&pkg->pkgchunk, &pkg->controllen);
	} else {
		char *key = package_primarykey(name, version);
		result = table_getrecord(target->packages, false, key,
				&pkg->pkgchunk, &pkg->controllen);
		free(key);
	}
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

retvalue package_openiterator(struct target *t, bool readonly, bool duplicate, /*@out@*/struct package_cursor *tc) {
	retvalue r, r2;
	struct cursor *c;

	if (verbose >= 15)
		fprintf(stderr, "trace: package_openiterator(target={identifier: %s}, readonly=%s, duplicate=%s) called.\n",
		        t->identifier, readonly ? "true" : "false", duplicate ? "true" : "false");

	tc->close_database = t->packages == NULL;
	r = target_initpackagesdb(t, readonly);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	r = table_newglobalcursor(t->packages, duplicate, &c);
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

retvalue package_openduplicateiterator(struct target *t, const char *name, long long skip, /*@out@*/struct package_cursor *tc) {
	retvalue r, r2;
	struct cursor *c;

	tc->close_database = t->packages == NULL;
	if (tc->close_database) {
		r = target_initpackagesdb(t, READONLY);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r))
			return r;
	}

	memset(&tc->current, 0, sizeof(tc->current));
	r = table_newduplicatecursor(t->packages, name, skip, &c, &tc->current.name,
	                             &tc->current.control, &tc->current.controllen);
	if (!RET_IS_OK(r)) {
		if (tc->close_database) {
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r, r2);
		}
		return r;
	}
	tc->current.target = t;
	tc->target = t;
	tc->cursor = c;
	return RET_OK;
}

bool package_next(struct package_cursor *tc) {
	bool success;

	if (verbose >= 15)
		fprintf(stderr, "trace: package_next(tc={current: {name: %s, version: %s}}) called.\n", tc->current.name, tc->current.version);

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
	if (tc->close_database) {
		r = target_closepackagesdb(tc->target);
		RET_UPDATE(result, r);
	} else {
		tc->target = NULL;
	}
	return result;
}

retvalue package_getversion(struct package *package) {
	retvalue r;

	if (package->version != NULL)
		return RET_OK;

	r = package->target->getversion(package->control, &package->pkgversion);
	if (RET_IS_OK(r)) {
		assert (package->pkgversion != NULL);
		package->version = package->pkgversion;
	}
	return r;
}

retvalue package_getarchitecture(struct package *package) {
	if (atom_defined(package->architecture))
		return RET_OK;

	return package->target->getarchitecture(package->control,
			&package->architecture);
}

retvalue package_getsource(struct package *package) {
	retvalue r;

	if (package->source != NULL)
		return RET_OK;

	r = package->target->getsourceandversion(package->control, package->name,
			&package->pkgsource, &package->pkgsrcversion);
	if (RET_IS_OK(r)) {
		assert (package->pkgsource != NULL);
		package->source = package->pkgsource;
		package->sourceversion = package->pkgsrcversion;
	}
	return r;
}
