/*  This file is part of "reprepro"
 *  Copyright (C) 2009 Bernhard R. Link
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
#include "strlist.h"
#include "indexfile.h"
#include "dpkgversions.h"
#include "target.h"
#include "distribution.h"
#include "tracking.h"
#include "files.h"
#include "archallflood.h"

struct aa_source_package {
	/*@null@*/struct aa_source_package *parent;
	/*@null@*/struct aa_source_package *left_child;
	/*@null@*/struct aa_source_package *right_child;
	/*@null@*/struct aa_source_package *nextversion;

	char *name;
	char *version;

	/* if true, it was already verified that there is no
	 * binary package of the same source version already there,
	 * so new architecture 'all' can be added without danger */
	bool has_no_sibling;
	/* if true, then there is a binary package of this source
	 * package, so replacing an architecture all is only allowed
	 * if there is already a binary for the new one */
	bool has_sibling;

	int refcount;
};

struct aa_package_data {
	struct aa_package_data *next;
	/* the name of the architecture all package: */
	char *name;

	/* NULL if does not exists/not yet known */
	/*@null@*/char *old_version;
	/*@null@*/struct aa_source_package *old_source;
	/*@null@*/char *new_version;
	/*@null@*/struct aa_source_package *new_source;
	bool new_has_sibling;

	struct checksumsarray new_origfiles;
	struct strlist new_filekeys;
	char *new_control;
};

struct floodlist {
	/*@dependent@*/struct target *target;
	struct aa_source_package *sources;
	struct aa_package_data *list;
	/* package the next package will most probably be after.
	 * (NULL=before start of list) */
	/*@null@*//*@dependent@*/struct aa_package_data *last;
};

static void aa_package_data_free(/*@only@*/struct aa_package_data *data){
	if (data == NULL)
		return;
	free(data->name);
	free(data->old_version);
	free(data->new_version);
	free(data->new_control);
	strlist_done(&data->new_filekeys);
	checksumsarray_done(&data->new_origfiles);
	free(data);
}

static void floodlist_free(struct floodlist *list) {
	struct aa_source_package *s;
	struct aa_package_data *l;

	if (list == NULL)
		return;

	l = list->list;
	while (l != NULL) {
		struct aa_package_data *n = l->next;
		aa_package_data_free(l);
		l = n;
	}
	s = list->sources;
	while (s != NULL) {
		struct aa_source_package *n;

		while (s->left_child != NULL || s->right_child != NULL) {
			if (s->left_child != NULL) {
				n = s->left_child;
				s->left_child = NULL;
				s = n;
			} else {
				n = s->right_child;
				s->right_child = NULL;
				s = n;
			}
		}

		while (s->nextversion != NULL) {
			n = s->nextversion->nextversion;
			/* do not free name, it is not malloced */
			free(s->nextversion->version);
			free(s->nextversion);
			s->nextversion = n;
		}
	        n = s->parent;
		free(s->name);
		free(s->version);
		free(s);
		s = n;
	}
	free(list);
	return;
}

static retvalue find_or_add_source(struct floodlist *list, /*@only@*/char *source, /*@only@*/char *sourceversion, /*@out@*/struct aa_source_package **src_p) {
	struct aa_source_package *parent, **p, *n;
	int c;

	parent = NULL;
	p = &list->sources;

	/* if this gets too slow, make it a balanced tree,
	 * but it seems fast enough even as simple tree */

	while (*p != NULL) {
		c = strcmp(source, (*p)->name);
		if (c == 0)
			break;
		parent = *p;
		if (c > 0)
			p = &parent->right_child;
		else
			p = &parent->left_child;
	}
	if (*p == NULL) {
		/* there is not even something with this name */
		n = zNEW(struct aa_source_package);
		if (FAILEDTOALLOC(n)) {
			free(source); free(sourceversion);
			return RET_ERROR_OOM;
		}
		n->name = source;
		n->version = sourceversion;
		n->parent = parent;
		*p = n;
		*src_p = n;
		return RET_OK;
	}
	free(source);
	source = (*p)->name;
	/* source name found, now look for version: */
	c = strcmp(sourceversion, (*p)->version);
	if (c == 0) {
		free(sourceversion);
		*src_p = *p;
		return RET_OK;
	}
	if (c < 0) {
		/* before first item, do some swapping as this is
		 * part of the name linked list */
		n = zNEW(struct aa_source_package);
		if (FAILEDTOALLOC(n)) {
			free(sourceversion);
			return RET_ERROR_OOM;
		}
		memcpy(n, *p, sizeof(struct aa_source_package));
		setzero(struct aa_source_package, *p);
		(*p)->name = source;
		(*p)->version = sourceversion;
		(*p)->left_child = n->left_child;
		(*p)->right_child = n->right_child;
		(*p)->parent = n->parent;
		n->left_child = NULL;
		n->right_child = NULL;
		n->parent = NULL;
		(*p)->nextversion = n;
		*src_p = *p;
		return RET_OK;
	}
	do {
		p = &(*p)->nextversion;
		if (*p == NULL)
			break;
		c = strcmp(sourceversion, (*p)->version);
	} while (c > 0);

	if (c == 0) {
		assert (*p != NULL);
		free(sourceversion);
		*src_p = *p;
		return RET_OK;
	}
	n = zNEW(struct aa_source_package);
	if (FAILEDTOALLOC(n)) {
		free(sourceversion);
		return RET_ERROR_OOM;
	}
	n->name = source;
	n->version = sourceversion;
	n->nextversion = *p;
	*p = n;
	*src_p = n;
	return RET_OK;
}

static struct aa_source_package *find_source(struct floodlist *list, const char *source, const char *sourceversion) {
	struct aa_source_package *p;
	int c = -1;

	p = list->sources;

	while (p != NULL) {
		c = strcmp(source, p->name);
		if (c == 0)
			break;
		if (c > 0)
			p = p->right_child;
		else
			p = p->left_child;
	}
	if (p == NULL)
		return NULL;
	while (p != NULL && (c = strcmp(sourceversion, p->version)) > 0)
		p = p->nextversion;
	if (c < 0)
		return NULL;
	else
		return p;
}

/* Before anything else is done the current state of one target is read into
 * the list: list->list points to the first in the sorted list,
 * list->last to the last one inserted */
static retvalue save_package_version(struct floodlist *list, const char *packagename, const char *chunk) {
	char *version, *source, *sourceversion;
	architecture_t architecture;
	struct aa_source_package *src;
	retvalue r;
	struct aa_package_data *package;

	r = list->target->getarchitecture(chunk, &architecture);
	if (RET_WAS_ERROR(r))
		return r;

	r = list->target->getsourceandversion(chunk, packagename,
			&source, &sourceversion);
	if (RET_WAS_ERROR(r))
		return r;

	r = find_or_add_source(list, source, sourceversion, &src);
	source = NULL; sourceversion = NULL; // just to be sure
	if (RET_WAS_ERROR(r))
		return r;

	r = list->target->getversion(chunk, &version);
	if (RET_WAS_ERROR(r))
		return r;


	if (architecture != architecture_all) {
		free(version);
		src->has_sibling = true;
		return RET_NOTHING;
	}

	package = zNEW(struct aa_package_data);
	if (FAILEDTOALLOC(package)) {
		free(version);
		return RET_ERROR_OOM;
	}

	package->name = strdup(packagename);
	if (FAILEDTOALLOC(package->name)) {
		free(package);
		free(version);
		return RET_ERROR_OOM;
	}
	package->old_version = version;
	version = NULL; // just to be sure...
	package->old_source = src;

	if (list->list == NULL) {
		/* first chunk to add: */
		list->list = package;
		list->last = package;
	} else {
		if (strcmp(packagename, list->last->name) > 0) {
			list->last->next = package;
			list->last = package;
		} else {
			/* this should only happen if the underlying
			 * database-method get changed, so just throwing
			 * out here */
			fprintf(stderr,
"INTERNAL ERROR: Package database is not sorted!!!\n");
			assert(false);
			exit(EXIT_FAILURE);
		}
	}
	return RET_OK;
}

static retvalue floodlist_initialize(struct floodlist **fl, struct target *t) {
	struct floodlist *list;
	retvalue r, r2;
	const char *packagename, *controlchunk;
	struct target_cursor iterator;

	list = zNEW(struct floodlist);
	if (FAILEDTOALLOC(list))
		return RET_ERROR_OOM;

	list->target = t;

	/* Begin with the packages currently in the archive */

	r = target_openiterator(t, READONLY, &iterator);
	if (RET_WAS_ERROR(r)) {
		floodlist_free(list);
		return r;
	}
	while (target_nextpackage(&iterator, &packagename, &controlchunk)) {
		r2 = save_package_version(list, packagename, controlchunk);
		RET_UPDATE(r, r2);
		if (RET_WAS_ERROR(r2))
			break;
	}
	r2 = target_closeiterator(&iterator);
	RET_UPDATE(r, r2);

	if (RET_WAS_ERROR(r)) {
		floodlist_free(list);
		return r;
	}
	list->last = NULL;
	*fl = list;
	return RET_OK;
}

static retvalue floodlist_trypackage(struct floodlist *list, const char *packagename_const, /*@only@*/char *version, const char *chunk) {
	retvalue r;
	struct aa_package_data *current, *insertafter;

	/* insertafter = NULL will mean insert before list */
	insertafter = list->last;
	/* the next one to test, current = NULL will mean not found */
	if (insertafter != NULL)
		current = insertafter->next;
	else
		current = list->list;

	/* the algorithm assumes almost all packages are feed in
	 * alphabetically. */

	while (true) {
		int cmp;

		assert (insertafter == NULL || insertafter->next == current);
		assert (insertafter != NULL || current == list->list);

		if (current == NULL)
			cmp = -1; /* every package is before the end of list */
		else
			cmp = strcmp(packagename_const, current->name);

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
			precmp = strcmp(packagename_const, insertafter->name);
			if (precmp == 0) {
				current = insertafter;
				break;
			} else if (precmp < 0) {
				/* restart at the beginning: */
				current = list->list;
				insertafter = NULL;
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
		struct aa_package_data *new;
		char *source, *sourceversion;
		struct aa_source_package *src;

		r = list->target->getsourceandversion(chunk,
				packagename_const, &source, &sourceversion);
		if (! RET_IS_OK(r)) {
			free(version);
			return r;
		}
		src = find_source(list, source, sourceversion);
		free(source); free(sourceversion);
		new = zNEW(struct aa_package_data);
		if (FAILEDTOALLOC(new)) {
			free(version);
			return RET_ERROR_OOM;
		}
		new->new_source = src;
		new->new_version = version;
		version = NULL;
		new->name = strdup(packagename_const);
		if (FAILEDTOALLOC(new->name)) {
			aa_package_data_free(new);
			return RET_ERROR_OOM;
		}
		r = list->target->getinstalldata(list->target,
				new->name, new->new_version,
				architecture_all, chunk,
				&new->new_control, &new->new_filekeys,
				&new->new_origfiles);
		if (RET_WAS_ERROR(r)) {
			aa_package_data_free(new);
			return r;
		}
		if (insertafter != NULL) {
			new->next = insertafter->next;
			insertafter->next = new;
		} else {
			new->next = list->list;
			list->list = new;
		}
		list->last = new;
	} else {
		/* The package already exists: */
		char *control;
		struct strlist files;
		struct checksumsarray origfiles;
		char *source, *sourceversion;
		struct aa_source_package *src;
		int versioncmp;

		list->last = current;

		if (current->new_has_sibling) {
			/* it has a new and that has a binary sibling,
			 * which means this becomes the new version
			 * exactly when it is newer than the old newest */
			r = dpkgversions_cmp(version, current->new_version,
					&versioncmp);
			if (RET_WAS_ERROR(r)) {
				free(version);
				return r;
			}
			if (versioncmp <= 0) {
				free(version);
				return RET_NOTHING;
			}
		} else if (current->old_version != NULL) {
			/* if it is older than the old one, we will
			 * always discard it */
			r = dpkgversions_cmp(version, current->old_version,
					&versioncmp);
			if (RET_WAS_ERROR(r)) {
				free(version);
				return r;
			}
			if (versioncmp <= 0) {
				free(version);
				return RET_NOTHING;
			}
		}
		/* we need to get the source to know more */

		r = list->target->getsourceandversion(chunk,
				packagename_const, &source, &sourceversion);
		if (! RET_IS_OK(r)) {
			free(version);
			return r;
		}
		src = find_source(list, source, sourceversion);
		free(source); free(sourceversion);
		if (src == NULL || !src->has_sibling) {
			/* the new one has no sibling, only allowed
			 * to override those that have: */
			if (current->new_version == NULL) {
				if (current->old_source->has_sibling) {
					free(version);
					return RET_NOTHING;
				}
			} else if (current->new_has_sibling) {
				free(version);
				return RET_NOTHING;
			} else {
				/* the new one has no sibling and the old one
				 * has not too, take the newer one: */
				r = dpkgversions_cmp(version,
						current->new_version,
						&versioncmp);
				if (RET_WAS_ERROR(r)) {
					free(version);
					return r;
				}
				if (versioncmp <= 0) {
					free(version);
					return RET_NOTHING;
				}
			}
		}

		r = list->target->getinstalldata(list->target,
				packagename_const, version,
				architecture_all, chunk,
				&control, &files, &origfiles);
		if (RET_WAS_ERROR(r)) {
			free(version);
			return r;
		}
		free(current->new_version);
		current->new_version = version;
		current->new_source = src;
		current->new_has_sibling = src != NULL && src->has_sibling;
		strlist_done(&current->new_filekeys);
		strlist_move(&current->new_filekeys, &files);
		checksumsarray_done(&current->new_origfiles);
		checksumsarray_move(&current->new_origfiles, &origfiles);
		free(current->new_control);
		current->new_control = control;
	}
	return RET_OK;
}

static retvalue floodlist_pull(struct floodlist *list, struct target *source) {
	retvalue result, r;
	const char *package, *control;
	struct target_cursor iterator;

	list->last = NULL;
	r = target_openiterator(source, READONLY, &iterator);
	if (RET_WAS_ERROR(r))
		return r;
	result = RET_NOTHING;
	while (target_nextpackage(&iterator, &package, &control)) {
		char *version;
		architecture_t package_architecture;

		r = list->target->getarchitecture(control,
				&package_architecture);
		if (r == RET_NOTHING)
			continue;
		if (!RET_IS_OK(r)) {
			RET_UPDATE(result, r);
			break;
		}
		if (package_architecture != architecture_all)
			continue;

		r = list->target->getversion(control, &version);
		if (r == RET_NOTHING)
			continue;
		if (!RET_IS_OK(r)) {
			RET_UPDATE(result, r);
			break;
		}
		r = floodlist_trypackage(list, package, version, control);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		if (interrupted()) {
			result = RET_ERROR_INTERRUPTED;
			break;
		}
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

static retvalue floodlist_install(struct floodlist *list, struct logger *logger, /*@NULL@*/struct trackingdata *td) {
	struct aa_package_data *pkg;
	retvalue result, r;

	if (list->list == NULL)
		return RET_NOTHING;

	result = target_initpackagesdb(list->target, READWRITE);
	if (RET_WAS_ERROR(result))
		return result;
	result = RET_NOTHING;
	for (pkg = list->list ; pkg != NULL ; pkg = pkg->next) {
		if (pkg->new_version != NULL) {
			r = files_expectfiles(&pkg->new_filekeys,
					pkg->new_origfiles.checksums);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				continue;
			if (interrupted()) {
				r = RET_ERROR_INTERRUPTED;
				break;
			}
			if (td != NULL) {
				if (pkg->new_source != NULL) {
					r = trackingdata_switch(td,
						pkg->new_source->name,
						pkg->new_source->version);
				} else {
					char *source, *sourceversion;

					r = list->target->getsourceandversion(
							pkg->new_control,
							pkg->name,
							&source,
							&sourceversion);
					assert (r != RET_NOTHING);
					if (RET_WAS_ERROR(r)) {
						RET_UPDATE(result, r);
						break;
					}
					r = trackingdata_switch(td,
							source, sourceversion);
					free(source);
					free(sourceversion);
				}
				if (RET_WAS_ERROR(r)) {
					RET_UPDATE(result, r);
					break;
				}
			}
			r = target_addpackage(list->target,
					logger, pkg->name, pkg->new_version,
					pkg->new_control, &pkg->new_filekeys,
					false, td, architecture_all,
					NULL, NULL, NULL);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
	}
	r = target_closepackagesdb(list->target);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue flood(struct distribution *d, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, architecture_t architecture, trackingdb tracks) {
	struct target *t, *s;
	retvalue result = RET_NOTHING, r;
	struct trackingdata trackingdata;

	if (tracks != NULL) {
		r = trackingdata_new(tracks, &trackingdata);
		if (RET_WAS_ERROR(r))
			return r;
	}

	for (t = d->targets ; t != NULL ; t = t->next) {
		struct floodlist *fl = NULL;

		if (atom_defined(architecture)) {
			if (architecture != t->architecture)
				continue;
		} else if (limitations_missed(architectures,
					t->architecture))
				continue;
		if (limitations_missed(components, t->component))
			continue;
		if (limitations_missed(packagetypes, t->packagetype))
			continue;
		if (t->packagetype != pt_deb && t->packagetype != pt_udeb)
			continue;

		r = floodlist_initialize(&fl, t);
		if (RET_WAS_ERROR(r)) {
			if (tracks != NULL)
				trackingdata_done(&trackingdata);
			return r;
		}

		for (s = d->targets ; s != NULL ; s = s->next) {
			if (s->component != t->component)
				continue;
			if (s->packagetype != t->packagetype)
				continue;
			/* no need to copy things from myself: */
			if (s->architecture == t->architecture)
				continue;
			if (limitations_missed(architectures,
						s->architecture))
				continue;
			r = floodlist_pull(fl, s);
			RET_UPDATE(d->status, r);
			if (RET_WAS_ERROR(r)) {
				if (tracks != NULL)
					trackingdata_done(&trackingdata);
				floodlist_free(fl);
				return r;
			}
		}
		r = floodlist_install(fl, d->logger,
				(tracks != NULL)?&trackingdata:NULL);
		RET_UPDATE(result, r);
		floodlist_free(fl);
		if (RET_WAS_ERROR(r)) {
			if (tracks != NULL)
				trackingdata_done(&trackingdata);
			return r;
		}
	}
	if (tracks != NULL) {
		r = trackingdata_finish(tracks, &trackingdata);
		RET_ENDUPDATE(result, r);
	}
	return result;
}
