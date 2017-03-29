/*  This file is part of "reprepro"
 *  Copyright (C) 2008,2009,2016 Bernhard R. Link
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

#include <sys/types.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include "error.h"
#include "ignore.h"
#include "strlist.h"
#include "indexfile.h"
#include "files.h"
#include "target.h"
#include "terms.h"
#include "termdecide.h"
#include "dpkgversions.h"
#include "tracking.h"
#include "filecntl.h"
#include "mprintf.h"
#include "globmatch.h"
#include "package.h"
#include "copypackages.h"

struct target_package_list {
	struct target_package_list *next;
	struct target *target;
	struct target *fromtarget;
	struct selectedpackage {
		/*@null@*/struct selectedpackage *next;
		char *name;
		char *version;
		char *sourcename;
		char *sourceversion;
		char *control;
		struct checksumsarray origfiles;
		struct strlist filekeys;
		architecture_t architecture;
	} *packages;
};

struct package_list {
	/*@null@*/struct target_package_list *targets;
};

// cascade_strcmp compares the two strings s1 and s2. If the strings are equal, the strings
// t1 and t2 are compared.
static int cascade_strcmp(const char *s1, const char *s2, const char *t1, const char *t2) {
	int result;

	result = strcmp(s1, s2);
	if (result == 0) {
		result = strcmp(t1, t2);
	}
	return result;
}

static retvalue list_newpackage(struct package_list *list, struct target *desttarget, struct target *fromtarget, const char *sourcename, const char *sourceversion, const char *packagename, const char *packageversion, /*@out@*/struct selectedpackage **package_p) {
	struct target_package_list *t, **t_p;
	struct selectedpackage *package, **p_p;
	int c;

	t_p = &list->targets;
	while (*t_p != NULL && (*t_p)->target != desttarget && (*t_p)->fromtarget != fromtarget)
		t_p = &(*t_p)->next;
	if (*t_p == NULL) {
		t = zNEW(struct target_package_list);
		if (FAILEDTOALLOC(t))
			return RET_ERROR_OOM;
		t->target = desttarget;
		t->fromtarget = fromtarget;
		t->next = *t_p;
		*t_p = t;
	} else
		t = *t_p;

	p_p = &t->packages;
	while (*p_p != NULL && (c = cascade_strcmp(packagename, (*p_p)->name, packageversion, (*p_p)->version)) < 0)
		p_p = &(*p_p)->next;
	if (*p_p != NULL && c == 0) {
		// TODO: improve this message..., or some context elsewhere
		fprintf(stderr, "Multiple occurrences of package '%s' with version '%s'!\n",
				packagename, packageversion);
		return RET_ERROR_EXIST;
	}
	package = zNEW(struct selectedpackage);
	if (FAILEDTOALLOC(package))
		return RET_ERROR_OOM;
	package->name = strdup(packagename);
	if (FAILEDTOALLOC(package->name)) {
		free(package);
		return RET_ERROR_OOM;
	}
	package->version = strdup(packageversion);
	if (FAILEDTOALLOC(package->version)) {
		free(package->name);
		free(package);
		return RET_ERROR_OOM;
	}
	package->sourcename = strdup(sourcename);
	if (FAILEDTOALLOC(package->sourcename)) {
		free(package->name);
		free(package->version);
		free(package);
		return RET_ERROR_OOM;
	}
	package->sourceversion = strdup(sourceversion);
	if (FAILEDTOALLOC(package->sourceversion)) {
		free(package->name);
		free(package->version);
		free(package->sourcename);
		free(package);
		return RET_ERROR_OOM;
	}
	package->next = *p_p;
	*p_p = package;
	*package_p = package;
	return RET_OK;
}

static void package_free(/*@only@*/struct selectedpackage *package) {
	if (package == NULL)
		return;

	free(package->name);
	free(package->version);
	free(package->sourcename);
	free(package->sourceversion);
	free(package->control);
	checksumsarray_done(&package->origfiles);
	strlist_done(&package->filekeys);
	free(package);
}

static void list_cancelpackage(struct package_list *list, /*@only@*/struct selectedpackage *package) {
	struct target_package_list *target;
	struct selectedpackage **p_p;

	assert (package != NULL);

	for (target = list->targets ; target != NULL ; target = target->next) {
		p_p = &target->packages;
		while (*p_p != NULL && *p_p != package)
			p_p = &(*p_p)->next;
		if (*p_p == package) {
			*p_p = package->next;
			package_free(package);
			return;
		}
	}
	assert (package == NULL);
}

static retvalue list_prepareadd(struct package_list *list, struct target *desttarget, struct target *fromtarget, struct package *package) {
	struct selectedpackage *new SETBUTNOTUSED(= NULL);
	retvalue r;
	int i;

	assert (desttarget->packagetype == package->target->packagetype);

	r = package_getversion(package);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	r = package_getarchitecture(package);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	r = package_getsource(package);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	r = list_newpackage(list, desttarget, fromtarget,
			package->source, package->sourceversion,
			package->name, package->version, &new);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	assert (new != NULL);

	new->architecture = package->architecture;
	r = desttarget->getinstalldata(desttarget, package,
			&new->control, &new->filekeys, &new->origfiles);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		list_cancelpackage(list, new);
		return r;
	}
	assert (new->filekeys.count == new->origfiles.names.count);
	for (i = 0 ; i < new->filekeys.count ; i++) {
		const char *newfilekey = new->filekeys.values[i];
		const char *oldfilekey = new->origfiles.names.values[i];
		const struct checksums *checksums = new->origfiles.checksums[i];

		r = files_canadd(newfilekey, checksums);
		/* normally it should just already have that file,
		 * in which case we have nothing to do: */
		if (r == RET_NOTHING)
			continue;
		/* otherwise try to cope with it */
		if (r == RET_ERROR_WRONG_MD5) {
			if (strcmp(newfilekey, oldfilekey) == 0) {
				fprintf(stderr,
"Error: package %s version %s lists different checksums than in the pool!\n",
					new->name, new->version);
			} else {
				fprintf(stderr,
"Error: package %s version %s needs '%s' which previously was '%s',\n"
"but the new file is already listed with different checksums!\n",
					new->name, new->version,
					newfilekey, oldfilekey);
			}
		}
		if (RET_WAS_ERROR(r)) {
			list_cancelpackage(list, new);
			return r;
		}
		assert (RET_IS_OK(r));
		if (strcmp(newfilekey, oldfilekey) == 0) {
			fprintf(stderr,
"Error: package %s version %s lists file %s not yet in the pool!\n",
				new->name, new->version, newfilekey);
			list_cancelpackage(list, new);
			return RET_ERROR_MISSING;
		}
		// TODO:
		// check new
		// - if exists and other checksums delete
		// - if exists and correct checksums use
		// otherwise check old
		// - if exists and other checksums bail out
		// - if exists and correct checksum, hardlink/copy
		fprintf(stderr,
"Error: cannot yet deal with files changing their position\n"
"(%s vs %s in %s version %s)\n",
					newfilekey, oldfilekey,
					new->name, new->version);
		list_cancelpackage(list, new);
		return RET_ERROR_MISSING;
	}
	return RET_OK;
}

static retvalue package_add(struct distribution *into, /*@null@*/trackingdb tracks, struct target *target, const struct selectedpackage *package, /*@null@*/ struct distribution *from, /*@null@*/trackingdb fromtracks, struct target *fromtarget, bool remove_source) {
	struct trackingdata trackingdata;
	retvalue r;

	if (verbose >= 1) {
		printf("Adding '%s' '%s' to '%s'.\n",
				package->name, package->version,
				target->identifier);
	}

	r = files_expectfiles(&package->filekeys,
			package->origfiles.checksums);
	if (RET_WAS_ERROR(r))
		return r;
	if (interrupted())
		return RET_ERROR_INTERRUPTED;
	if (tracks != NULL) {
		r = trackingdata_summon(tracks, package->sourcename,
				package->version, &trackingdata);
		if (RET_WAS_ERROR(r))
			return r;
	}
	r = target_addpackage(target,
			into->logger,
			package->name, package->version,
			package->control,
			&package->filekeys, true,
			(tracks != NULL)?
			&trackingdata:NULL,
			package->architecture,
			NULL, from != NULL ? from->codename : NULL);
	RET_UPDATE(into->status, r);

	if (tracks != NULL) {
		retvalue r2;

		r2 = trackingdata_finish(tracks, &trackingdata);
		RET_ENDUPDATE(r, r2);
	}

	if (!RET_WAS_ERROR(r) && remove_source) {
		if (fromtracks != NULL) {
			r = trackingdata_summon(fromtracks, package->sourcename,
					package->version, &trackingdata);
			if (RET_WAS_ERROR(r))
				return r;
		}
		r = target_removepackage(fromtarget,
				from->logger,
				package->name, package->version,
				(tracks != NULL) ? &trackingdata : NULL);
		RET_UPDATE(from->status, r);
		if (fromtracks != NULL) {
			retvalue r2;

			r2 = trackingdata_finish(fromtracks, &trackingdata);
			RET_ENDUPDATE(r, r2);
		}
	}
	return r;
}

static retvalue packagelist_add(struct distribution *into, const struct package_list *list, /*@null@*/struct distribution *from, bool remove_source) {
	retvalue result, r;
	struct target_package_list *tpl;
	struct selectedpackage *package;
	trackingdb tracks, fromtracks = NULL;

	if (verbose >= 15)
		fprintf(stderr, "trace: packagelist_add(into.codename=%s, from.codename=%s) called.\n",
		        into->codename, from != NULL ? from->codename : NULL);

	r = distribution_prepareforwriting(into);
	if (RET_WAS_ERROR(r))
		return r;

	if (remove_source) {
		r = distribution_prepareforwriting(from);
		if (RET_WAS_ERROR(r))
			return r;
	}

	if (into->tracking != dt_NONE) {
		r = tracking_initialize(&tracks, into, false);
		if (RET_WAS_ERROR(r))
			return r;
	} else
		tracks = NULL;

	if (from->tracking != dt_NONE) {
		r = tracking_initialize(&fromtracks, from, false);
		if (RET_WAS_ERROR(r))
			return r;
	}

	result = RET_NOTHING;
	for (tpl = list->targets; tpl != NULL ; tpl = tpl->next) {
		struct target *target = tpl->target;
		struct target *fromtarget = tpl->fromtarget;

		if (verbose >= 15)
			fprintf(stderr, "trace: Processing add/move from '%s' to '%s'...\n",
			        fromtarget != NULL ? fromtarget->identifier : NULL, target->identifier);

		r = target_initpackagesdb(target, READWRITE);
		RET_ENDUPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;

		if (remove_source) {
			r = target_initpackagesdb(fromtarget, READWRITE);
			RET_ENDUPDATE(result, r);
			if (RET_WAS_ERROR(r)) {
				(void)target_closepackagesdb(target);
				break;
			}
		}

		for (package = tpl->packages; package != NULL ;
		                              package = package->next) {
			r = package_add(into, tracks, target,
					package, from, fromtracks, fromtarget, remove_source);
			RET_UPDATE(result, r);
		}
		if (remove_source) {
			r = target_closepackagesdb(fromtarget);
			RET_UPDATE(into->status, r);
			RET_ENDUPDATE(result, r);
		}
		r = target_closepackagesdb(target);
		RET_UPDATE(into->status, r);
		RET_ENDUPDATE(result, r);
	}
	r = tracking_done(fromtracks, from);
	RET_ENDUPDATE(result, r);
	r = tracking_done(tracks, into);
	RET_ENDUPDATE(result, r);
	return result;
}

static retvalue copy_by_func(struct package_list *list, struct distribution *into, struct distribution *from, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, retvalue action(struct package_list*, struct target *, struct target *, void *), void *data) {
	retvalue result, r;
	struct target *origtarget, *desttarget;

	result = RET_NOTHING;
	for (origtarget = from->targets ; origtarget != NULL ;
			origtarget = origtarget->next) {
		if (!target_matches(origtarget,
				components, architectures, packagetypes))
			continue;
		desttarget = distribution_gettarget(into,
				origtarget->component,
				origtarget->architecture,
				origtarget->packagetype);
		if (desttarget == NULL) {
			if (verbose > 2)
				printf(
"Not looking into '%s' as no matching target in '%s'!\n",
					origtarget->identifier,
					into->codename);
			continue;
		}
		r = action(list, desttarget, origtarget, data);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(result))
			return result;
	}
	return result;
}

struct namelist {
	int argc;
	const char **argv;
	bool *warnedabout;
	bool *found;
};

static retvalue by_name(struct package_list *list, struct target *desttarget, struct target *fromtarget, void *data) {
	struct nameandversion *nameandversion = data;
	struct nameandversion *prev;
	retvalue result, r;

	result = RET_NOTHING;
	for (struct nameandversion *d = nameandversion; d->name != NULL ; d++) {
		struct package package;

		for (prev = nameandversion ; prev < d ; prev++) {
			if (strcmp(prev->name, d->name) == 0 && strcmp2(prev->version, d->version) == 0)
				break;
		}
		if (prev < d) {
			if (verbose >= 0 && ! prev->warnedabout) {
				if (d->version == NULL) {
					fprintf(stderr,
"Hint: '%s' was listed multiple times, ignoring all but first!\n",
							d->name);
				} else {
					fprintf(stderr,
"Hint: '%s=%s' was listed multiple times, ignoring all but first!\n",
							d->name, d->version);
				}
			}
			prev->warnedabout = true;
			/* do not complain second is missing if we ignore it: */
			d->found = true;
			continue;
		}

		r = package_get(fromtarget, d->name, d->version, &package);
		if (r == RET_NOTHING)
			continue;
		RET_ENDUPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		r = list_prepareadd(list, desttarget, fromtarget, &package);
		package_done(&package);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		d->found = true;
	}
	return result;
}

static void packagelist_done(struct package_list *list) {
	struct target_package_list *target;
	struct selectedpackage *package;

	while ((target = list->targets) != NULL) {
		list->targets = target->next;

		while ((package = target->packages) != NULL) {
			target->packages = package->next;

			package_free(package);
		}
		free(target);
	}
}

retvalue copy_by_name(struct distribution *into, struct distribution *from, struct nameandversion *nameandversion, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, bool remove_source) {
	struct package_list list;
	retvalue r;

	for (struct nameandversion *d = nameandversion; d->name != NULL; d++) {
		d->found = false;
		d->warnedabout = false;
	}

	memset(&list, 0, sizeof(list));
	r = copy_by_func(&list, into, from, components,
			architectures, packagetypes, by_name, nameandversion);
	if (verbose >= 0 && !RET_WAS_ERROR(r)) {
		bool first = true;

		for (struct nameandversion *d = nameandversion; d->name != NULL; d++) {
			if (d->found)
				continue;
			if (first)
				(void)fputs(
"Will not copy as not found: ", stderr);
			else
				(void)fputs(", ", stderr);
			first = false;
			(void)fputs(d->name, stderr);
			if (d->version != NULL) {
				(void)fputs("=", stderr);
				(void)fputs(d->version, stderr);
			}
		}
		if (!first) {
			(void)fputc('.', stderr);
			(void)fputc('\n', stderr);
		}
	}
	if (!RET_IS_OK(r))
		return r;
	r = packagelist_add(into, &list, from, remove_source);
	packagelist_done(&list);
	return r;
}

static retvalue by_source(struct package_list *list, struct target *desttarget, struct target *fromtarget, void *data) {
	struct namelist *d = data;
	struct package_cursor iterator;
	retvalue result, r;

	assert (d->argc > 0);

	r = package_openiterator(fromtarget, READONLY, true, &iterator);
	assert (r != RET_NOTHING);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (package_next(&iterator)) {
		int i;

		r = package_getsource(&iterator.current);
		if (r == RET_NOTHING)
			continue;
		if (RET_WAS_ERROR(r)) {
			result = r;
			break;
		}
		/* only include if source name matches */
		if (strcmp(iterator.current.source, d->argv[0]) != 0) {
			continue;
		}
		i = 0;
		if (d->argc > 1) {
			int c;

			i = d->argc;
			while (--i > 0) {
				r = dpkgversions_cmp(
						iterator.current.sourceversion,
						d->argv[i], &c);
				assert (r != RET_NOTHING);
				if (RET_WAS_ERROR(r)) {
					(void)package_closeiterator(&iterator);
					return r;
				}
				if (c == 0)
					break;
			}
			/* there are source versions specified and
			 * the source version of this package differs */
			if (i == 0) {
				continue;
			}
		}
		r = list_prepareadd(list, desttarget, fromtarget, &iterator.current);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		d->found[0] = true;
		d->found[i] = true;
	}
	r = package_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue copy_by_source(struct distribution *into, struct distribution *from, int argc, const char **argv, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, bool remove_source) {
	struct package_list list;
	struct namelist names = { argc, argv, NULL, nzNEW(argc, bool) };
	retvalue r;

	if (FAILEDTOALLOC(names.found)) {
		free(names.found);
		return RET_ERROR_OOM;
	}
	memset(&list, 0, sizeof(list));
	// TODO: implement fast way by looking at source tracking
	// (also allow copying .changes and .logs)
	r = copy_by_func(&list, into, from, components, architectures,
			packagetypes, by_source, &names);
	if (argc == 1 && !RET_WAS_ERROR(r) && verbose >= 0) {
		assert(names.found != NULL);

		if (!names.found[0]) {
			assert (r == RET_NOTHING);
			fprintf(stderr,
"Nothing to do as no package with source '%s' found!\n",
				       argv[0]);
			free(names.found);
			return RET_NOTHING;
		}
	} else if (!RET_WAS_ERROR(r) && verbose >= 0) {
		int i;
		bool first = true, anything = false;

		for (i = 1 ; i < argc ; i++) {
			if (names.found[i])
				anything = true;
		}
		if (!anything) {
			assert (r == RET_NOTHING);
			fprintf(stderr,
"Nothing to do as no packages with source '%s' and a requested source version found!\n",
				       argv[0]);
			free(names.found);
			return RET_NOTHING;
		}
		for (i = 1 ; i < argc ; i++) {
			if (names.found[i])
				continue;
			if (first)
				(void)fputs(
"Will not copy as not found: ", stderr);
			else
				(void)fputs(", ", stderr);
			first = false;
			(void)fputs(argv[i], stderr);
		}
		if (!first) {
			(void)fputc('.', stderr);
			(void)fputc('\n', stderr);
		}
		if (verbose > 5) {
			(void)fputs("Found versions are: ", stderr);
			first = true;
			for (i = 1 ; i < argc ; i++) {
				if (!names.found[i])
					continue;
				if (!first)
					(void)fputs(", ", stderr);
				first = false;
				(void)fputs(argv[i], stderr);
			}
			(void)fputc('.', stderr);
			(void)fputc('\n', stderr);
		}
	}
	free(names.found);
	if (!RET_IS_OK(r))
		return r;
	r = packagelist_add(into, &list, from, remove_source);
	packagelist_done(&list);
	return r;
}

static retvalue by_formula(struct package_list *list, struct target *desttarget, struct target *fromtarget, void *data) {
	term *condition = data;
	struct package_cursor iterator;
	retvalue result, r;

	r = package_openiterator(fromtarget, READONLY, true, &iterator);
	assert (r != RET_NOTHING);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (package_next(&iterator)) {
		r = term_decidepackage(condition, &iterator.current,
				desttarget);
		if (r == RET_NOTHING)
			continue;
		if (RET_WAS_ERROR(r)) {
			result = r;
			break;
		}
		r = list_prepareadd(list, desttarget, fromtarget, &iterator.current);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}
	r = package_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

static retvalue by_glob(struct package_list *list, struct target *desttarget, struct target *fromtarget, void *data) {
	const char *glob = data;
	struct package_cursor iterator;
	retvalue result, r;

	r = package_openiterator(fromtarget, READONLY, true, &iterator);
	assert (r != RET_NOTHING);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (package_next(&iterator)) {
		if (!globmatch(iterator.current.name, glob))
			continue;
		r = list_prepareadd(list, desttarget, fromtarget, &iterator.current);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}
	r = package_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue copy_by_glob(struct distribution *into, struct distribution *from, const char *glob, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, bool remove_source) {
	struct package_list list;
	retvalue r;

	memset(&list, 0, sizeof(list));

	r = copy_by_func(&list, into, from, components, architectures,
			packagetypes, by_glob, (void*)glob);
	if (!RET_IS_OK(r))
		return r;
	r = packagelist_add(into, &list, from, remove_source);
	packagelist_done(&list);
	return r;
}

retvalue copy_by_formula(struct distribution *into, struct distribution *from, const char *filter, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, bool remove_source) {
	struct package_list list;
	term *condition;
	retvalue r;

	memset(&list, 0, sizeof(list));

	r = term_compilefortargetdecision(&condition, filter);
	if (!RET_IS_OK(r)) {
		return r;
	}
	r = copy_by_func(&list, into, from, components, architectures,
			packagetypes, by_formula, condition);
	term_free(condition);
	if (!RET_IS_OK(r))
		return r;
	r = packagelist_add(into, &list, from, remove_source);
	packagelist_done(&list);
	return r;
}

static retvalue choose_by_name(struct package *package, void *privdata) {
	const struct namelist *l = privdata;
	int i;

	for (i = 0 ; i < l->argc ; i++) {
		if (strcmp(package->name, l->argv[i]) == 0)
			break;
	}
	if (i >= l->argc)
		return RET_NOTHING;
	return RET_OK;
}

static retvalue choose_by_source(struct package *package, void *privdata) {
	const struct namelist *l = privdata;
	retvalue r;

	r = package_getsource(package);
	if (!RET_IS_OK(r))
		return r;

	assert (l->argc > 0);
	/* only include if source name matches */
	if (strcmp(package->source, l->argv[0]) != 0) {
		return RET_NOTHING;
	}
	if (l->argc > 1) {
		int i, c;

		i = l->argc;
		while (--i > 0) {
			r = dpkgversions_cmp(package->sourceversion,
					l->argv[i], &c);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r)) {
				return r;
			}
			if (c == 0)
				break;
		}
		/* there are source versions specified and
		 * the source version of this package differs */
		if (i == 0) {
			return RET_NOTHING;
		}
	}
	return RET_OK;
}

static retvalue choose_by_condition(struct package *package, void *privdata) {
	term *condition = privdata;

	return term_decidepackage(condition, package, package->target);
}

static retvalue choose_by_glob(struct package *package, void *privdata) {
	const char *glob = privdata;

	if (globmatch(package->name, glob))
		return RET_OK;
	else
		return RET_NOTHING;
}

retvalue copy_from_file(struct distribution *into, component_t component, architecture_t architecture, packagetype_t packagetype, const char *filename, int argc, const char **argv) {
	struct indexfile *i;
	retvalue result, r;
	struct target *target;
	struct package_list list;
	struct namelist d = {argc, argv, NULL, NULL};
	struct package package;

	assert (atom_defined(architecture));
	assert (atom_defined(component));
	assert (atom_defined(packagetype));

	memset(&list, 0, sizeof(list));
	target = distribution_gettarget(into,
			component, architecture, packagetype);
	if (target == NULL) {
		if (!atomlist_in(&into->architectures, architecture)) {
			fprintf(stderr,
"Distribution '%s' does not contain architecture '%s!'\n",
					into->codename,
					atoms_architectures[architecture]);
		}
		if (packagetype == pt_ddeb) {
			if (!atomlist_in(&into->ddebcomponents, component)) {
				fprintf(stderr,
"Distribution '%s' does not contain ddeb component '%s!'\n",
					into->codename,
					atoms_components[component]);
			}
		} else if (packagetype != pt_udeb) {
			if (!atomlist_in(&into->components, component)) {
				fprintf(stderr,
"Distribution '%s' does not contain component '%s!'\n",
					into->codename,
					atoms_components[component]);
			}
		} else {
			if (!atomlist_in(&into->udebcomponents, component)) {
				fprintf(stderr,
"Distribution '%s' does not contain udeb component '%s!'\n",
					into->codename,
					atoms_components[component]);
			}
		}
		/* -A source needing -T dsc and vice versa already checked
		 * in main.c */
		fprintf(stderr,
"No matching part of distribution '%s' found!\n",
						into->codename);
		return RET_ERROR;
	}
	result = indexfile_open(&i, filename, c_none);
	if (!RET_IS_OK(result))
		return result;
	result = RET_NOTHING;
	setzero(struct package, &package);
	while (indexfile_getnext(i, &package, target, false)) {
		r = choose_by_name(&package, &d);
		if (RET_IS_OK(r))
			r = list_prepareadd(&list, target, NULL, &package);
		package_done(&package);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(result))
			break;
	}
	r = indexfile_close(i);
	RET_ENDUPDATE(result, r);
	if (RET_IS_OK(result))
		result = packagelist_add(into, &list, NULL, false);
	packagelist_done(&list);
	return result;
}

typedef retvalue chooseaction(struct package *, void *);

static retvalue restore_from_snapshot(struct distribution *into, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, const char *snapshotname, chooseaction action, void *d) {
	retvalue result, r;
	struct package_list list;
	struct target *target;
	char *basedir;
	enum compression compression;
	struct distribution pseudo_from; // just stores the codename

	basedir = calc_snapshotbasedir(into->codename, snapshotname);
	if (FAILEDTOALLOC(basedir))
		return RET_ERROR_OOM;

	memset(&list, 0, sizeof(list));
	result = RET_NOTHING;
	for (target = into->targets ; target != NULL ;
			target = target->next) {
		struct package package;
		char *filename;
		struct indexfile *i;

		if (!target_matches(target,
				components, architectures, packagetypes))
			continue;

		/* we do not know what compressions where used back then
		 * and not even how the file was named, just look for
		 * how the file is named now and try all readable
		 * compressions */

		compression = c_none;
		filename = calc_dirconcat3(
				basedir, target->relativedirectory,
				target->exportmode->filename);
		if (filename != NULL && !isregularfile(filename)) {
			/* no uncompressed file found, try .gz */
			free(filename);
			compression = c_gzip;
			filename = mprintf("%s/%s/%s.gz",
					basedir, target->relativedirectory,
					target->exportmode->filename);
		}
#ifdef HAVE_LIBBZ2
		if (filename != NULL && !isregularfile(filename)) {
			/* no uncompressed or .gz file found, try .bz2 */
			free(filename);
			compression = c_bzip2;
			filename = mprintf("%s/%s/%s.bz2",
					basedir, target->relativedirectory,
					target->exportmode->filename);
		}
#endif
		if (filename != NULL && !isregularfile(filename)) {
			free(filename);
			fprintf(stderr,
"Could not find '%s/%s/%s' nor '%s/%s/%s.gz',\n"
"ignoring that part of the snapshot.\n",
					basedir, target->relativedirectory,
					target->exportmode->filename,
					basedir, target->relativedirectory,
					target->exportmode->filename);
			continue;
		}
		if (FAILEDTOALLOC(filename)) {
			result = RET_ERROR_OOM;
			break;
		}
		result = indexfile_open(&i, filename, compression);
		if (!RET_IS_OK(result))
			break;
		setzero(struct package, &package);
		while (indexfile_getnext(i, &package, target, false)) {
			result = action(&package, d);
			if (RET_IS_OK(result))
				result = list_prepareadd(&list,
						target, NULL, &package);
			package_done(&package);
			if (RET_WAS_ERROR(result))
				break;
		}
		r = indexfile_close(i);
		RET_ENDUPDATE(result, r);
		free(filename);
		if (RET_WAS_ERROR(result))
			break;
	}
	free(basedir);
	if (RET_WAS_ERROR(result))
		return result;
	memset(&pseudo_from, 0, sizeof(struct distribution));
	pseudo_from.codename = (char*)snapshotname;
	r = packagelist_add(into, &list, &pseudo_from, false);
	packagelist_done(&list);
	return r;
}

retvalue restore_by_name(struct distribution *into, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, const char *snapshotname, int argc, const char **argv) {
	struct namelist d = {argc, argv, NULL, NULL};
	return restore_from_snapshot(into,
			components, architectures, packagetypes,
			snapshotname, choose_by_name, &d);
}

retvalue restore_by_source(struct distribution *into, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, const char *snapshotname, int argc, const char **argv) {
	struct namelist d = {argc, argv, NULL, NULL};
	return restore_from_snapshot(into,
			components, architectures, packagetypes,
			snapshotname, choose_by_source, &d);
}

retvalue restore_by_formula(struct distribution *into, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, const char *snapshotname, const char *filter) {
	term *condition;
	retvalue r;

	r = term_compilefortargetdecision(&condition, filter);
	if (!RET_IS_OK(r)) {
		return r;
	}
	r = restore_from_snapshot(into,
			components, architectures, packagetypes,
			snapshotname, choose_by_condition, condition);
	term_free(condition);
	return r;
}

retvalue restore_by_glob(struct distribution *into, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, const char *snapshotname, const char *glob) {
	return restore_from_snapshot(into,
			components, architectures, packagetypes,
			snapshotname, choose_by_glob, (void*)glob);
}
