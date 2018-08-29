/*  This file is part of "reprepro"
 *  Copyright (C) 2010,2011,2016 Bernhard R. Link
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
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "error.h"
#include "distribution.h"
#include "trackingt.h"
#include "package.h"
#include "sourcecheck.h"

/* This is / will be the implementation of the
 *	unusedsources
 *	withoutsource
 *	reportcruft
 *	removecruft (to be implemented)
 * commands.
 *
 * Currently those only work with tracking enabled, but
 * are in this file as the implementation without tracking
 * will need similar infrastructure */


/* TODO: some tree might be more efficient, check how bad the comparisons are here */
struct info_source {
	struct info_source *next;
	char *name;
	struct info_source_version {
		struct info_source_version *next;
		char *version;
		bool used;
	} version;
};

static void free_source_info(struct info_source *s) {
	while (s != NULL) {
		struct info_source *h = s;
		s = s->next;

		while (h->version.next != NULL) {
			struct info_source_version *v = h->version.next;
			h->version.next = v->next;
			free(v->version);
			free(v);
		}
		free(h->version.version);
		free(h->name);
		free(h);
	}
}

static retvalue collect_source_versions(struct distribution *d, struct info_source **out) {
	struct info_source *root = NULL, *last = NULL;
	struct target *t;
	struct package_cursor cursor;
	retvalue result = RET_NOTHING, r;

	for (t = d->targets ; t != NULL ; t = t->next) {
		if (t->architecture != architecture_source)
			continue;
		r = package_openiterator(t, true, true, &cursor);
		if (RET_WAS_ERROR(r)) {
			RET_UPDATE(result, r);
			break;
		}
		while (package_next(&cursor)) {
			struct info_source **into = NULL;
			struct info_source_version *v;

			r = package_getversion(&cursor.current);
			if (!RET_IS_OK(r)) {
				RET_UPDATE(result, r);
				continue;
			}
			if (last != NULL) {
				int c;
				c = strcmp(cursor.current.name, last->name);
				if (c < 0) {
					/* start at the beginning */
					last = NULL;
				} else while (c > 0) {
					into = &last->next;
					if (last->next == NULL)
						break;
					last = last->next;
					c = strcmp(cursor.current.name, last->name);
					if (c == 0) {
						into = NULL;
						break;
					}
			       }
			}
			/* if into != NULL, place there,
			 * if last != NULL, already found */
			if (last == NULL) {
				into = &root;
				while ((last = *into) != NULL) {
					int c;
					c = strcmp(cursor.current.name, last->name);
					if (c == 0) {
						into = NULL;
						break;
					}
					if (c < 0)
						break;
					into = &last->next;
				}
			}
			if (into != NULL) {
				last = zNEW(struct info_source);
				if (FAILEDTOALLOC(last)) {
					result = RET_ERROR_OOM;
					break;
				}
				last->name = strdup(cursor.current.name);
				if (FAILEDTOALLOC(last->name)) {
					free(last);
					result = RET_ERROR_OOM;
					break;
				}
				last->version.version = package_dupversion(
						&cursor.current);
				if (FAILEDTOALLOC(last->version.version)) {
					result = RET_ERROR_OOM;
					free(last->name);
					free(last);
					break;
				}
				last->next = *into;
				*into = last;
				RET_UPDATE(result, RET_OK);
				continue;
			}
			assert (last != NULL);
			assert (strcmp(cursor.current.name, last->name)==0);

			v = &last->version;
			while (strcmp(v->version, cursor.current.version) != 0) {
				if (v->next == NULL) {
					v->next = zNEW(struct info_source_version);
					if (FAILEDTOALLOC(v->next)) {
						result = RET_ERROR_OOM;
						break;
					}
					v = v->next;
					v->version = package_dupversion(
							&cursor.current);
					if (FAILEDTOALLOC(v->version)) {
						result = RET_ERROR_OOM;
						break;
					}
					RET_UPDATE(result, RET_OK);
					break;
				}
				v = v->next;
			}
		}
		r = package_closeiterator(&cursor);
		if (RET_WAS_ERROR(r)) {
			RET_UPDATE(result, r);
			break;
		}
	}
	if (RET_IS_OK(result))
		*out = root;
	else {
		assert (result != RET_NOTHING || root == NULL);
		free_source_info(root);
	}
	return result;
}

static retvalue process_binaries(struct distribution *d, struct info_source *sources, retvalue (*action)(struct package *, void *), void *privdata) {
	struct target *t;
	struct package_cursor cursor;
	retvalue result = RET_NOTHING, r;

	for (t = d->targets ; t != NULL ; t = t->next) {
		if (t->architecture == architecture_source)
			continue;
		r = package_openiterator(t, true, true, &cursor);
		if (RET_WAS_ERROR(r)) {
			RET_UPDATE(result, r);
			break;
		}
		while (package_next(&cursor)) {
			struct info_source *s;
			struct info_source_version *v;

			r = package_getsource(&cursor.current);
			if (!RET_IS_OK(r)) {
				RET_UPDATE(result, r);
				continue;
			}
			const char *source = cursor.current.source;
			const char *version = cursor.current.sourceversion;

			s = sources;
			while (s != NULL && strcmp(s->name, source) < 0) {
				s = s->next;
			}
			if (s != NULL && strcmp(source, s->name) == 0) {
				v = &s->version;
				while (v != NULL && strcmp(version, v->version) != 0)
					v = v->next;
			} else
				v = NULL;
			if (v != NULL) {
				v->used = true;
			} else if (action != NULL) {
				r = action(&cursor.current, privdata);
				RET_UPDATE(result, r);
			}
		}
		r = package_closeiterator(&cursor);
		if (RET_WAS_ERROR(r)) {
			RET_UPDATE(result, r);
			break;
		}
	}
	return result;
}

static retvalue listunusedsources(struct distribution *d, const struct trackedpackage *pkg) {
	bool hasbinary = false, hassource = false;
	int i;

	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		if (pkg->refcounts[i] == 0)
			continue;
		if (pkg->filetypes[i] == 's')
			hassource = true;
		if (pkg->filetypes[i] == 'b')
			hasbinary = true;
		if (pkg->filetypes[i] == 'a')
			hasbinary = true;
	}
	if (hassource && ! hasbinary) {
		printf("%s %s %s\n", d->codename, pkg->sourcename,
				pkg->sourceversion);
		return RET_OK;
	}
	return RET_NOTHING;
}

retvalue unusedsources(struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for (d = alldistributions ; d != NULL ; d = d->next) {
		if (!d->selected)
			continue;
		if (!atomlist_in(&d->architectures, architecture_source))
			continue;
		if (d->tracking != dt_NONE) {
			r = tracking_foreach_ro(d, listunusedsources);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				return r;
			continue;
		}
		struct info_source *sources = NULL;
		const struct info_source *s;
		const struct info_source_version *v;

		r = collect_source_versions(d, &sources);
		if (!RET_IS_OK(r))
			continue;

		r = process_binaries(d, sources, NULL, NULL);
		RET_UPDATE(result, r);
		for (s = sources ; s != NULL ; s = s->next) {
			for (v = &s->version ; v != NULL ; v = v->next) {
				if (v->used)
					continue;
				printf("%s %s %s\n", d->codename,
						s->name, v->version);
			}
		}
		free_source_info(sources);
	}
	return result;
}

static retvalue listsourcemissing(struct distribution *d, const struct trackedpackage *pkg) {
	bool hasbinary = false, hassource = false;
	int i;

	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		if (pkg->refcounts[i] == 0)
			continue;
		if (pkg->filetypes[i] == 's')
			hassource = true;
		if (pkg->filetypes[i] == 'b')
			hasbinary = true;
		if (pkg->filetypes[i] == 'a')
			hasbinary = true;
	}
	if (hasbinary && ! hassource) {
		for (i = 0 ; i < pkg->filekeys.count ; i++) {
			if (pkg->refcounts[i] == 0)
				continue;
			if (pkg->filetypes[i] != 'b' && pkg->filetypes[i] != 'a')
				continue;
			printf("%s %s %s %s\n", d->codename, pkg->sourcename,
					pkg->sourceversion,
					pkg->filekeys.values[i]);
		}
		return RET_OK;
	}
	return RET_NOTHING;
}

static retvalue listmissing(struct package *package, UNUSED(void*data)) {
	retvalue r;
	struct strlist list;

	r = package->target->getfilekeys(package->control, &list);
	if (!RET_IS_OK(r))
		return r;
	assert (list.count == 1);
	printf("%s %s %s %s\n", package->target->distribution->codename,
			package->source, package->sourceversion, list.values[0]);
	strlist_done(&list);
	return RET_OK;
}

retvalue sourcemissing(struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for (d = alldistributions ; d != NULL ; d = d->next) {
		if (!d->selected)
			continue;
		if (!atomlist_in(&d->architectures, architecture_source)) {
			if (verbose >= 0)
				fprintf(stderr,
"Not processing distribution '%s', as it has no source packages.\n",
						d->codename);
			continue;
		}
		if (d->tracking != dt_NONE) {
			r = tracking_foreach_ro(d, listsourcemissing);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				return r;
		} else {
			struct info_source *sources = NULL;

			r = collect_source_versions(d, &sources);
			if (!RET_IS_OK(r))
				continue;

			r = process_binaries(d, sources, listmissing, NULL);
			RET_UPDATE(result, r);
			free_source_info(sources);
		}

	}
	return result;
}

static retvalue listcruft(struct distribution *d, const struct trackedpackage *pkg) {
	bool hasbinary = false, hassource = false;
	int i;

	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		if (pkg->refcounts[i] == 0)
			continue;
		if (pkg->filetypes[i] == 's')
			hassource = true;
		if (pkg->filetypes[i] == 'b')
			hasbinary = true;
		if (pkg->filetypes[i] == 'a')
			hasbinary = true;
	}
	if (hasbinary && ! hassource) {
		printf("binaries-without-source %s %s %s\n", d->codename,
				pkg->sourcename, pkg->sourceversion);
		return RET_OK;
	} else if (hassource && ! hasbinary) {
		printf("source-without-binaries %s %s %s\n", d->codename,
				pkg->sourcename, pkg->sourceversion);
		return RET_OK;
	}
	return RET_NOTHING;
}

static retvalue listmissingonce(struct package *package, void *data) {
	struct info_source **already = data;
	struct info_source *s;

	for (s = *already ; s != NULL ; s = s->next) {
		if (strcmp(s->name, package->source) != 0)
			continue;
		if (strcmp(s->version.version, package->sourceversion) != 0)
			continue;
		return RET_NOTHING;
	}
	s = zNEW(struct info_source);
	if (FAILEDTOALLOC(s))
		return RET_ERROR_OOM;
	s->name = strdup(package->source);
	s->version.version = strdup(package->sourceversion);
	if (FAILEDTOALLOC(s->name) || FAILEDTOALLOC(s->version.version)) {
		free(s->name);
		free(s->version.version);
		free(s);
		return RET_ERROR_OOM;
	}
	s->next = *already;
	*already = s;
	printf("binaries-without-source %s %s %s\n",
			package->target->distribution->codename,
			package->source, package->sourceversion);
	return RET_OK;
}

retvalue reportcruft(struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for (d = alldistributions ; d != NULL ; d = d->next) {
		if (!d->selected)
			continue;
		if (!atomlist_in(&d->architectures, architecture_source)) {
			if (verbose >= 0)
				fprintf(stderr,
"Not processing distribution '%s', as it has no source packages.\n",
						d->codename);
			continue;
		}
		if (d->tracking != dt_NONE) {
			r = tracking_foreach_ro(d, listcruft);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				return r;
			continue;
		}
		struct info_source *sources = NULL;
		struct info_source *list = NULL;
		const struct info_source *s;
		const struct info_source_version *v;

		r = collect_source_versions( d, &sources);
		if (!RET_IS_OK(r))
			continue;

		r = process_binaries( d, sources,
				listmissingonce, &list);
		RET_UPDATE(result, r);
		for (s = sources ; s != NULL ; s = s->next) {
			for (v = &s->version ; v != NULL ; v = v->next) {
				if (v->used)
					continue;
				printf("source-without-binaries %s %s %s\n",
					d->codename, s->name, v->version);
			}
		}
		free_source_info(list);
		free_source_info(sources);
	}
	return result;
}
