/*  This file is part of "reprepro"
 *  Copyright (C) 2009,2016 Bernhard R. Link
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
#include "package.h"
#include "archallflood.h"

struct aa_source_name {
	/*@null@*/struct aa_source_name *parent;
	/*@null@*/struct aa_source_name *left_child;
	/*@null@*/struct aa_source_name *right_child;

	char *name;

	/*@null@*/struct aa_source_version *versions;
};

struct aa_source_version {
	/*@null@*/struct aa_source_version *next;
	struct aa_source_name *name;
	char *version;

	/* if true, it was already verified that there is no
	 * binary package of the same source version already there,
	 * so new architecture 'all' can be added without danger */
	bool has_no_sibling;
	/* if true, then there is a binary package of this source
	 * package, so replacing an architecture all is only allowed
	 * if there is already a binary for the new one */
	bool has_sibling;
};

struct aa_package_data {
	struct aa_package_data *next;
	/* the name of the architecture all package: */
	char *name;

	/* NULL if does not exists/not yet known */
	/*@null@*/char *old_version;
	/*@null@*/struct aa_source_version *old_source;
	/*@null@*/char *new_version;
	/*@null@*/struct aa_source_version *new_source;
	bool new_has_sibling;

	struct checksumsarray new_origfiles;
	struct strlist new_filekeys;
	char *new_control;
};

struct floodlist {
	/*@dependent@*/struct target *target;
	struct aa_source_name *sources;
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
	struct aa_source_name *s;
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
		struct aa_source_name *n;

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

		while (s->versions != NULL) {
			struct aa_source_version *nv;
			nv = s->versions->next;
			free(s->versions->version);
			free(s->versions);
			s->versions = nv;
		}
	        n = s->parent;
		free(s->name);
		free(s);
		s = n;
	}
	free(list);
	return;
}

static retvalue find_or_add_sourcename(struct floodlist *list, struct package *pkg, /*@out@*/struct aa_source_name **src_p) {
	struct aa_source_name *parent, **p, *n;
	int c;

	parent = NULL;
	p = &list->sources;

	/* if this gets too slow, make it a balanced tree,
	 * but it seems fast enough even as simple tree */

	while (*p != NULL) {
		c = strcmp(pkg->source, (*p)->name);
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
		n = zNEW(struct aa_source_name);
		if (FAILEDTOALLOC(n)) {
			return RET_ERROR_OOM;
		}
		n->name = strdup(pkg->source);
		if (FAILEDTOALLOC(n->name)) {
			free(n);
			return RET_ERROR_OOM;
		}
		n->parent = parent;
		*p = n;
		*src_p = n;
		return RET_OK;
	}
	*src_p = *p;
	return RET_OK;
}

static retvalue find_or_add_source(struct floodlist *list, struct package *pkg, /*@out@*/struct aa_source_version **src_p) {
	retvalue r;
	struct aa_source_name *sn;
	struct aa_source_version **p, *n;
	int c;

	r = find_or_add_sourcename(list, pkg, &sn);
	if (RET_WAS_ERROR(r))
		return r;

	/* source name found (or created), now look for version: */

	p = &sn->versions;
	c = -1;
	while (*p != NULL && (c = strcmp(pkg->sourceversion,
					(*p)->version)) > 0) {
		p = &(*p)->next;
	}
	if (c == 0) {
		assert (*p != NULL);
		*src_p = *p;
		return RET_OK;
	}
	n = zNEW(struct aa_source_version);
	if (FAILEDTOALLOC(n)) {
		return RET_ERROR_OOM;
	}
	n->name = sn;
	n->version = strdup(pkg->sourceversion);
	if (FAILEDTOALLOC(n->version)) {
		free(n);
		return RET_ERROR_OOM;
	}
	n->next = *p;
	*p = n;
	*src_p = n;
	return RET_OK;
}

static struct aa_source_version *find_source(struct floodlist *list, const char *source, const char *sourceversion) {
	struct aa_source_name *p;
	struct aa_source_version *v;
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
	v = p->versions;
	while (v != NULL && (c = strcmp(sourceversion, v->version)) > 0)
		v = v->next;
	if (c < 0)
		return NULL;
	else
		return v;
}

/* Before anything else is done the current state of one target is read into
 * the list: list->list points to the first in the sorted list,
 * list->last to the last one inserted */
static retvalue save_package_version(struct floodlist *list, struct package *pkg) {
	struct aa_source_version *src;
	retvalue r;
	struct aa_package_data *package;

	r = package_getarchitecture(pkg);
	if (RET_WAS_ERROR(r))
		return r;

	r = package_getsource(pkg);
	if (RET_WAS_ERROR(r))
		return r;

	r = find_or_add_source(list, pkg, &src);
	if (RET_WAS_ERROR(r))
		return r;

	if (pkg->architecture != architecture_all) {
		src->has_sibling = true;
		return RET_NOTHING;
	}

	r = package_getversion(pkg);
	if (RET_WAS_ERROR(r))
		return r;

	package = zNEW(struct aa_package_data);
	if (FAILEDTOALLOC(package)) {
		return RET_ERROR_OOM;
	}

	package->name = strdup(pkg->name);
	if (FAILEDTOALLOC(package->name)) {
		free(package);
		return RET_ERROR_OOM;
	}
	package->old_version = package_dupversion(pkg);
	if (FAILEDTOALLOC(package->old_version)) {
		free(package->name);
		free(package);
		return RET_ERROR_OOM;
	}
	package->old_source = src;

	if (list->list == NULL) {
		/* first chunk to add: */
		list->list = package;
		list->last = package;
	} else {
		if (strcmp(pkg->name, list->last->name) > 0) {
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
	struct package_cursor iterator;

	list = zNEW(struct floodlist);
	if (FAILEDTOALLOC(list))
		return RET_ERROR_OOM;

	list->target = t;

	/* Begin with the packages currently in the archive */

	r = package_openiterator(t, READONLY, &iterator);
	if (RET_WAS_ERROR(r)) {
		floodlist_free(list);
		return r;
	}
	while (package_next(&iterator)) {
		r2 = save_package_version(list, &iterator.current);
		RET_UPDATE(r, r2);
		if (RET_WAS_ERROR(r2))
			break;
	}
	r2 = package_closeiterator(&iterator);
	RET_UPDATE(r, r2);

	if (RET_WAS_ERROR(r)) {
		floodlist_free(list);
		return r;
	}
	list->last = NULL;
	*fl = list;
	return RET_OK;
}

static retvalue floodlist_trypackage(struct floodlist *list, struct package *package) {
	retvalue r;
	struct aa_package_data *current, *insertafter;

	r = package_getversion(package);
	if (!RET_IS_OK(r))
		return r;
	r = package_getsource(package);
	if (!RET_IS_OK(r))
		return r;

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
			precmp = strcmp(package->name, insertafter->name);
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
		struct aa_source_version *src;

		src = find_source(list, package->source, package->sourceversion);
		new = zNEW(struct aa_package_data);
		if (FAILEDTOALLOC(new)) {
			return RET_ERROR_OOM;
		}
		new->new_source = src;
		new->new_version = package_dupversion(package);
		if (FAILEDTOALLOC(new->new_version)) {
			aa_package_data_free(new);
			return RET_ERROR_OOM;
		}
		new->name = strdup(package->name);
		if (FAILEDTOALLOC(new->name)) {
			aa_package_data_free(new);
			return RET_ERROR_OOM;
		}
		r = list->target->getinstalldata(list->target,
				package,
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
		struct aa_source_version *src;
		int versioncmp;

		list->last = current;

		if (current->new_has_sibling) {
			/* it has a new and that has a binary sibling,
			 * which means this becomes the new version
			 * exactly when it is newer than the old newest */
			r = dpkgversions_cmp(package->version,
					current->new_version,
					&versioncmp);
			if (RET_WAS_ERROR(r)) {
				return r;
			}
			if (versioncmp <= 0) {
				return RET_NOTHING;
			}
		} else if (current->old_version != NULL) {
			/* if it is older than the old one, we will
			 * always discard it */
			r = dpkgversions_cmp(package->version,
					current->old_version,
					&versioncmp);
			if (RET_WAS_ERROR(r)) {
				return r;
			}
			if (versioncmp <= 0) {
				return RET_NOTHING;
			}
		}
		/* we need to get the source to know more */

		src = find_source(list, package->source, package->sourceversion);
		if (src == NULL || !src->has_sibling) {
			/* the new one has no sibling, only allowed
			 * to override those that have: */
			if (current->new_version == NULL) {
				if (current->old_source->has_sibling)
					return RET_NOTHING;
			} else if (current->new_has_sibling) {
				return RET_NOTHING;
			} else {
				/* the new one has no sibling and the old one
				 * has not too, take the newer one: */
				r = dpkgversions_cmp(package->version,
						current->new_version,
						&versioncmp);
				if (RET_WAS_ERROR(r)) {
					return r;
				}
				if (versioncmp <= 0) {
					return RET_NOTHING;
				}
			}
		}
		char *new_version = package_dupversion(package);
		if (FAILEDTOALLOC(new_version))
			return RET_ERROR_OOM;

		r = list->target->getinstalldata(list->target,
				package,
				&control, &files, &origfiles);
		if (RET_WAS_ERROR(r)) {
			free(new_version);
			return r;
		}
		free(current->new_version);
		current->new_version = new_version;
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
	struct package_cursor iterator;

	list->last = NULL;
	r = package_openiterator(source, READONLY, &iterator);
	if (RET_WAS_ERROR(r))
		return r;
	result = RET_NOTHING;
	while (package_next(&iterator)) {
		r = package_getarchitecture(&iterator.current);
		if (r == RET_NOTHING)
			continue;
		if (!RET_IS_OK(r)) {
			RET_UPDATE(result, r);
			break;
		}
		if (iterator.current.architecture != architecture_all)
			continue;

		r = floodlist_trypackage(list, &iterator.current);
		RET_UPDATE(result, r);
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
						pkg->new_source->name->name,
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
					NULL, NULL);
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
