/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2006,2007,2008,2016 Bernhard R. Link
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "error.h"
#include "ignore.h"
#include "strlist.h"
#include "indexfile.h"
#include "dpkgversions.h"
#include "target.h"
#include "files.h"
#include "descriptions.h"
#include "package.h"
#include "upgradelist.h"

struct package_data {
	struct package_data *next;
	/* the name of the package: */
	char *name;
	/* the version in our repository:
	 * NULL means not yet in the archive */
	char *version_in_use;
	/* the most recent version we found
	 * (either is version_in_use or version_new)*/
	/*@dependent@*/const char *version;

	/* if this is != 0, package will be deleted afterwards,
	 * (or new version simply ignored if it is not yet in the
	 * archive) */
	bool deleted;

	/* The most recent version we found upstream:
	 * NULL means nothing found. */
	char *new_version;
	/* where the recent version comes from: */
	/*@dependent@*/void *privdata;

	/* the new control-chunk for the package to go in
	 * non-NULL if new_version && newversion == version_in_use */
	char *new_control;
	/* the list of files that will belong to this:
	 * same validity */
	struct strlist new_filekeys;
	struct checksumsarray new_origfiles;
	/* to destinguish arch all from not arch all */
	architecture_t architecture;
};

struct upgradelist {
	/*@dependent@*/struct target *target;
	struct package_data *list;
	/* package the next package will most probably be after.
	 * (NULL=before start of list) */
	/*@null@*//*@dependent@*/struct package_data *last;
	/* internal...*/
};

static void package_data_free(/*@only@*/struct package_data *data){
	if (data == NULL)
		return;
	free(data->name);
	free(data->version_in_use);
	free(data->new_version);
	//free(data->new_from);
	free(data->new_control);
	strlist_done(&data->new_filekeys);
	checksumsarray_done(&data->new_origfiles);
	free(data);
}

/* This is called before any package lists are read.
 * It is called once for every package we already have in this target.
 * upgrade->list points to the first in the sorted list,
 * upgrade->last to the last one inserted */
static retvalue save_package_version(struct upgradelist *upgrade, struct package *pkg) {
	retvalue r;
	struct package_data *package;

	r = package_getversion(pkg);
	if (RET_WAS_ERROR(r))
		return r;

	package = zNEW(struct package_data);
	if (FAILEDTOALLOC(package))
		return RET_ERROR_OOM;

	package->privdata = NULL;
	package->name = strdup(pkg->name);
	if (FAILEDTOALLOC(package->name)) {
		free(package);
		return RET_ERROR_OOM;
	}
	package->version_in_use = package_dupversion(pkg);
	if (FAILEDTOALLOC(package->version_in_use)) {
		free(package->name);
		free(package);
		return RET_ERROR_OOM;
	}
	package->version = package->version_in_use;

	if (upgrade->list == NULL) {
		/* first package to add: */
		upgrade->list = package;
		upgrade->last = package;
	} else {
		if (strcmp(pkg->name, upgrade->last->name) > 0) {
			upgrade->last->next = package;
			upgrade->last = package;
		} else {
			/* this should only happen if the underlying
			 * database-method get changed, so just throwing
			 * out here */
			fprintf(stderr, "Package database is not sorted!!!\n");
			assert(false);
			exit(EXIT_FAILURE);
		}
	}

	return RET_OK;
}

retvalue upgradelist_initialize(struct upgradelist **ul, struct target *t) {
	struct upgradelist *upgrade;
	retvalue r, r2;
	struct package_cursor iterator;

	upgrade = zNEW(struct upgradelist);
	if (FAILEDTOALLOC(upgrade))
		return RET_ERROR_OOM;

	upgrade->target = t;

	/* Beginn with the packages currently in the archive */

	r = package_openiterator(t, READONLY, &iterator);
	if (RET_WAS_ERROR(r)) {
		upgradelist_free(upgrade);
		return r;
	}
	while (package_next(&iterator)) {
		r2 = save_package_version(upgrade, &iterator.current);
		RET_UPDATE(r, r2);
		if (RET_WAS_ERROR(r2))
			break;
	}
	r2 = package_closeiterator(&iterator);
	RET_UPDATE(r, r2);

	if (RET_WAS_ERROR(r)) {
		upgradelist_free(upgrade);
		return r;
	}

	upgrade->last = NULL;

	*ul = upgrade;
	return RET_OK;
}

void upgradelist_free(struct upgradelist *upgrade) {
	struct package_data *l;

	if (upgrade == NULL)
		return;

	l = upgrade->list;
	while (l != NULL) {
		struct package_data *n = l->next;
		package_data_free(l);
		l = n;
	}

	free(upgrade);
	return;
}

static retvalue upgradelist_trypackage(struct upgradelist *upgrade, void *privdata, upgrade_decide_function *predecide, void *predecide_data, struct package *package) {
	char *version;
	retvalue r;
	upgrade_decision decision;
	struct package_data *current, *insertafter;


	if (package->architecture == architecture_all) {
		if (upgrade->target->packagetype == pt_dsc) {
			fputs("Internal error: trying to put binary ('all')"
					" package into source architecture!\n",
					stderr);
			return RET_ERROR_INTERNAL;
		}
	}

	version = package_dupversion(package);
	if (FAILEDTOALLOC(version))
		return RET_ERROR_OOM;

	/* insertafter = NULL will mean insert before list */
	insertafter = upgrade->last;
	/* the next one to test, current = NULL will mean not found */
	if (insertafter != NULL)
		current = insertafter->next;
	else
		current = upgrade->list;

	/* the algorithm assumes almost all packages are feed in
	 * alphabetically. So the next package will likely be quite
	 * after the last one. Otherwise we walk down the long list
	 * again and again... and again... and even some more...*/

	while (true) {
		int cmp;

		assert (insertafter == NULL || insertafter->next == current);
		assert (insertafter != NULL || current == upgrade->list);

		if (current == NULL)
			cmp = -1; /* every package is before the end of list */
		else
			cmp = strcmp(package->name, current->name);

		if (cmp == 0)
			break;

		if (cmp < 0) {
			int precmp;

			if (insertafter == NULL) {
				/* if we are before the first
				 * package, add us there...*/
				current = NULL;
				break;
			}
			// I only hope no one creates indices anti-sorted:
			precmp = strcmp(package->name, insertafter->name);
			if (precmp == 0) {
				current = insertafter;
				break;
			} else if (precmp < 0) {
				/* restart at the beginning: */
				current = upgrade->list;
				insertafter = NULL;
				if (verbose > 10) {
					fprintf(stderr, "restarting search...");
				}
				continue;
			} else { // precmp > 0
				/* insert after insertafter: */
				current = NULL;
				break;
			}
			assert ("This is not reached" == NULL);
		}
		/* cmp > 0 : may come later... */
		assert (current != NULL);
		insertafter = current;
		current = current->next;
		if (current == NULL) {
			/* add behind insertafter at end of list */
			break;
		}
		/* otherwise repeat until place found */
	}
	if (current == NULL) {
		/* adding a package not yet known */
		struct package_data *new;
		char *newcontrol;

		decision = predecide(predecide_data, upgrade->target,
				package, NULL);
		if (decision != UD_UPGRADE) {
			upgrade->last = insertafter;
			if (decision == UD_LOUDNO)
				fprintf(stderr,
"Loudly rejecting '%s' '%s' to enter '%s'!\n",
						package->name, version,
						upgrade->target->identifier);
			free(version);
			return (decision==UD_ERROR)?RET_ERROR:RET_NOTHING;
		}

		new = zNEW(struct package_data);
		if (FAILEDTOALLOC(new)) {
			free(version);
			return RET_ERROR_OOM;
		}
		new->deleted = false; //to be sure...
		new->privdata = privdata;
		new->name = strdup(package->name);
		if (FAILEDTOALLOC(new->name)) {
			free(version);
			free(new);
			return RET_ERROR_OOM;
		}
		new->new_version = version;
		new->version = version;
		new->architecture = package->architecture;
		version = NULL; //to be sure...
		r = upgrade->target->getinstalldata(upgrade->target,
				package,
				&new->new_control, &new->new_filekeys,
				&new->new_origfiles);
		if (RET_WAS_ERROR(r)) {
			package_data_free(new);
			return r;
		}
		/* apply override data */
		r = upgrade->target->doreoverride(upgrade->target,
				new->name, new->new_control, &newcontrol);
		if (RET_WAS_ERROR(r)) {
			package_data_free(new);
			return r;
		}
		if (RET_IS_OK(r)) {
			free(new->new_control);
			new->new_control = newcontrol;
		}
		if (insertafter != NULL) {
			new->next = insertafter->next;
			insertafter->next = new;
		} else {
			new->next = upgrade->list;
			upgrade->list = new;
		}
		upgrade->last = new;
	} else {
		/* The package already exists: */
		char *control, *newcontrol;
		struct strlist files;
		struct checksumsarray origfiles;
		int versioncmp;

		upgrade->last = current;

		r = dpkgversions_cmp(version, current->version, &versioncmp);
		if (RET_WAS_ERROR(r)) {
			free(version);
			return r;
		}
		if (versioncmp <= 0 && !current->deleted) {
			/* there already is a newer version, so
			 * doing nothing but perhaps updating what
			 * versions are around, when we are newer
			 * than yet known candidates... */
			int c = 0;

			if (current->new_version == current->version)
				c =versioncmp;
			else if (current->new_version == NULL)
				c = 1;
			else (void)dpkgversions_cmp(version,
					       current->new_version, &c);

			if (c > 0) {
				free(current->new_version);
				current->new_version = version;
			} else
				free(version);

			return RET_NOTHING;
		}
		if (versioncmp > 0 && verbose > 30)
			fprintf(stderr,
"'%s' from '%s' is newer than '%s' currently\n",
				version, package->name, current->version);
		decision = predecide(predecide_data, upgrade->target,
				package, current->version);
		if (decision != UD_UPGRADE) {
			if (decision == UD_LOUDNO)
				fprintf(stderr,
"Loudly rejecting '%s' '%s' to enter '%s'!\n",
						package->name, version,
						upgrade->target->identifier);
			/* Even if we do not install it, setting it on hold
			 * will keep it or even install from a mirror before
			 * the delete was applied */
			if (decision == UD_HOLD)
				current->deleted = false;
			free(version);
			/* while supersede will remove the current package */
			if (decision == UD_SUPERSEDE) {
				current->deleted = true;
				return RET_OK;
			}
			return (decision==UD_ERROR)?RET_ERROR:RET_NOTHING;
		}

		if (versioncmp == 0) {
		/* we are replacing a package with the same version,
		 * so we keep the old one for sake of speed. */
			if (current->deleted &&
				current->version != current->new_version) {
				/* remember the version for checkupdate/pull */
				free(current->new_version);
				current->new_version = version;
			} else
					free(version);
			current->deleted = false;
			return RET_NOTHING;
		}
		if (versioncmp != 0 && current->version == current->new_version
				&& current->version_in_use != NULL) {
			/* The version to include is not the newest after the
			 * last deletion round), but maybe older, maybe newer.
			 * So we get to the question: it is also not the same
			 * like the version we already have? */
			int vcmp = 1;
			(void)dpkgversions_cmp(version,
					current->version_in_use, &vcmp);
			if (vcmp == 0) {
				current->version = current->version_in_use;
				if (current->deleted) {
					free(current->new_version);
					current->new_version = version;
				} else
					free(version);
				current->deleted = false;
				return RET_NOTHING;
			}
		}

// TODO: the following case might be worth considering, but sadly new_version
// might have changed without the proper data set.
//		if (versioncmp >= 0 && current->version == current->version_in_use
//				&& current->new_version != NULL) 

		current->architecture = package->architecture;
		r = upgrade->target->getinstalldata(upgrade->target,
				package,
				&control, &files, &origfiles);
		if (RET_WAS_ERROR(r)) {
			free(version);
			return r;
		}
		/* apply override data */
		r = upgrade->target->doreoverride(upgrade->target,
				package->name, control, &newcontrol);
		if (RET_WAS_ERROR(r)) {
			free(version);
			free(control);
			strlist_done(&files);
			checksumsarray_done(&origfiles);
			return r;
		}
		if (RET_IS_OK(r)) {
			free(control);
			control = newcontrol;
		}
		current->deleted = false;
		free(current->new_version);
		current->new_version = version;
		current->version = version;
		current->privdata = privdata;
		strlist_move(&current->new_filekeys, &files);
		checksumsarray_move(&current->new_origfiles, &origfiles);
		free(current->new_control);
		current->new_control = control;
	}
	return RET_OK;
}

retvalue upgradelist_update(struct upgradelist *upgrade, void *privdata, const char *filename, upgrade_decide_function *decide, void *decide_data, bool ignorewrongarchitecture) {
	struct indexfile *i;
	struct package package;
	retvalue result, r;

	r = indexfile_open(&i, filename, c_none);
	if (!RET_IS_OK(r))
		return r;

	result = RET_NOTHING;
	upgrade->last = NULL;
	setzero(struct package, &package);
	while (indexfile_getnext(i, &package,
				upgrade->target, ignorewrongarchitecture)) {
		r = package_getsource(&package);
		if (RET_IS_OK(r)) {
			r = upgradelist_trypackage(upgrade, privdata,
					decide, decide_data, &package);
			RET_UPDATE(result, r);
		}
		package_done(&package);
		if (RET_WAS_ERROR(r)) {
			if (verbose > 0)
				fprintf(stderr,
"Stop reading further chunks from '%s' due to previous errors.\n", filename);
			break;
		}
		if (interrupted()) {
			result = RET_ERROR_INTERRUPTED;
			break;
		}
	}
	r = indexfile_close(i);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue upgradelist_pull(struct upgradelist *upgrade, struct target *source, upgrade_decide_function *predecide, void *decide_data, void *privdata) {
	retvalue result, r;
	struct package_cursor iterator;

	upgrade->last = NULL;
	r = package_openiterator(source, READONLY, &iterator);
	if (RET_WAS_ERROR(r))
		return r;
	result = RET_NOTHING;
	while (package_next(&iterator)) {
		assert (source->packagetype == upgrade->target->packagetype);

		r = package_getversion(&iterator.current);
		assert (r != RET_NOTHING);
		if (!RET_IS_OK(r)) {
			RET_UPDATE(result, r);
			break;
		}
		r = package_getarchitecture(&iterator.current);
		if (!RET_IS_OK(r)) {
			RET_UPDATE(result, r);
			break;
		}
		if (iterator.current.architecture != architecture_all &&
				iterator.current.architecture !=
					upgrade->target->architecture) {
			continue;
		}

		r = package_getsource(&iterator.current);
		if (RET_IS_OK(r)) {
			r = upgradelist_trypackage(upgrade, privdata,
					predecide, decide_data,
					&iterator.current);
			RET_UPDATE(result, r);
		}
		if (RET_WAS_ERROR(r))
			break;
		if (interrupted()) {
			result = RET_ERROR_INTERRUPTED;
			break;
		}
	}
	r = package_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

/* mark all packages as deleted, so they will vanis unless readded or reholded */
retvalue upgradelist_deleteall(struct upgradelist *upgrade) {
	struct package_data *pkg;

	for (pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next) {
		pkg->deleted = true;
	}

	return RET_OK;
}

/* request all wanted files in the downloadlists given before */
retvalue upgradelist_enqueue(struct upgradelist *upgrade, enqueueaction *action, void *calldata) {
	struct package_data *pkg;
	retvalue result, r;
	result = RET_NOTHING;
	assert(upgrade != NULL);
	for (pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next) {
		if (pkg->version == pkg->new_version && !pkg->deleted) {
			r = action(calldata, &pkg->new_origfiles,
					&pkg->new_filekeys, pkg->privdata);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
	}
	return result;
}

/* delete all packages that will not be kept (i.e. either deleted or upgraded) */
retvalue upgradelist_predelete(struct upgradelist *upgrade, struct logger *logger) {
	struct package_data *pkg;
	retvalue result, r;
	result = RET_NOTHING;
	assert(upgrade != NULL);

	result = target_initpackagesdb(upgrade->target, READWRITE);
	if (RET_WAS_ERROR(result))
		return result;
	for (pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next) {
		if (pkg->version_in_use != NULL &&
				(pkg->version == pkg->new_version
				 || pkg->deleted)) {
			if (interrupted())
				r = RET_ERROR_INTERRUPTED;
			else
				r = target_removepackage(upgrade->target,
						logger, pkg->name, NULL);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
	}
	r = target_closepackagesdb(upgrade->target);
	RET_ENDUPDATE(result, r);
	return result;
}

bool upgradelist_isbigdelete(const struct upgradelist *upgrade) {
	struct package_data *pkg;
	long long deleted = 0, all = 0;

	if (upgrade->list == NULL)
		return false;
	for (pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next) {
		if (pkg->version_in_use == NULL)
		       continue;
		all++;
		if (pkg->deleted)
			deleted++;
	}
	return deleted >= 10 && all/deleted < 5;
}

bool upgradelist_woulddelete(const struct upgradelist *upgrade) {
	struct package_data *pkg;

	if (upgrade->list == NULL)
		return false;
	for (pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next) {
		if (pkg->version_in_use == NULL)
		       continue;
		if (pkg->deleted)
			return true;
	}
	return false;
}

retvalue upgradelist_install(struct upgradelist *upgrade, struct logger *logger, bool ignoredelete, void (*callback)(void *, const char **, const char **)){
	struct package_data *pkg;
	retvalue result, r;

	if (upgrade->list == NULL)
		return RET_NOTHING;

	result = target_initpackagesdb(upgrade->target, READWRITE);
	if (RET_WAS_ERROR(result))
		return result;
	result = RET_NOTHING;
	for (pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next) {
		if (pkg->version == pkg->new_version && !pkg->deleted) {
			char *newcontrol;

			assert ((pkg->architecture == architecture_all &&
				 upgrade->target->packagetype != pt_dsc)
				|| pkg->architecture ==
					upgrade->target->architecture);

			r = files_checkorimprove(&pkg->new_filekeys,
					pkg->new_origfiles.checksums);
			if (! RET_WAS_ERROR(r)) {

				r = upgrade->target->completechecksums(
						pkg->new_control,
						&pkg->new_filekeys,
						pkg->new_origfiles.checksums,
						&newcontrol);
				assert (r != RET_NOTHING);
			}
			if (! RET_WAS_ERROR(r)) {
				/* upgrade (or possibly downgrade) */
				const char *causingrule = NULL,
				      *suitefrom = NULL;

				free(pkg->new_control);
				pkg->new_control = newcontrol;
				newcontrol = NULL;
				callback(pkg->privdata,
						&causingrule, &suitefrom);
// TODO: trackingdata?
				if (interrupted())
					r = RET_ERROR_INTERRUPTED;
				else
					r = target_addpackage(upgrade->target,
						logger, pkg->name,
						pkg->new_version,
						pkg->new_control,
						&pkg->new_filekeys, true,
						NULL, pkg->architecture,
						causingrule, suitefrom);
			}
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
		if (pkg->deleted && pkg->version_in_use != NULL
				&& !ignoredelete) {
			if (interrupted())
				r = RET_ERROR_INTERRUPTED;
			else
				r = target_removepackage(upgrade->target,
						logger, pkg->name, NULL);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
	}
	r = target_closepackagesdb(upgrade->target);
	RET_ENDUPDATE(result, r);
	return result;
}

void upgradelist_dump(struct upgradelist *upgrade, dumpaction action){
	struct package_data *pkg;

	assert(upgrade != NULL);

	for (pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next) {
		if (interrupted())
			return;
		if (pkg->deleted)
			action(pkg->name, pkg->version_in_use,
					NULL, pkg->new_version,
					NULL, NULL, pkg->privdata);
		else if (pkg->version == pkg->version_in_use)
			action(pkg->name, pkg->version_in_use,
					pkg->version_in_use, pkg->new_version,
					NULL, NULL, pkg->privdata);
		else
			action(pkg->name, pkg->version_in_use,
					pkg->new_version, NULL,
					&pkg->new_filekeys, pkg->new_control,
					pkg->privdata);
	}
}
