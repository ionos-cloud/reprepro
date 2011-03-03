/*  This file is part of "reprepro"
 *  Copyright (C) 2010,2011 Bernhard R. Link
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

static retvalue collect_source_versions(struct database *database, struct distribution *d, struct info_source **out) {
	struct info_source *root = NULL, *last = NULL;
	struct target *t;
	struct target_cursor target_cursor = TARGET_CURSOR_ZERO;
	const char *name, *chunk;
	retvalue result = RET_NOTHING, r;

	for (t = d->targets ; t != NULL ; t = t->next) {
		if (t->architecture_atom != architecture_source)
			continue;
		r = target_openiterator(t, database, true, &target_cursor);
		if (RET_WAS_ERROR(r)) {
			RET_UPDATE(result, r);
			break;
		}
		while (target_nextpackage(&target_cursor, &name, &chunk)) {
			char *version;
			struct info_source **into = NULL;
			struct info_source_version *v;

			r = t->getversion(chunk, &version);
			if (!RET_IS_OK(r)) {
				RET_UPDATE(result, r);
				continue;
			}
			if (last != NULL) {
				int c;
				c = strcmp(name, last->name);
				if (c < 0) {
					/* start at the beginning */
					last = NULL;
				} else while (c > 0) {
					into = &last->next;
					if (last->next == NULL)
						break;
					last = last->next;
					c = strcmp(name, last->name);
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
					c = strcmp(name, last->name);
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
				last = calloc(1, sizeof(struct info_source));
				if (FAILEDTOALLOC(last)) {
					free(version);
					result = RET_ERROR_OOM;
					break;
				}
				last->name = strdup(name);
				if (FAILEDTOALLOC(last->name)) {
					free(version);
					free(last);
					result = RET_ERROR_OOM;
					break;
				}
				last->version.version = version;
				last->next = *into;
				*into = last;
				RET_UPDATE(result, RET_OK);
				continue;
			}
			assert (last != NULL);
			assert (strcmp(name,last->name)==0);

			v = &last->version;
			while (strcmp(v->version, version) != 0) {
				if (v->next == NULL) {
					v->next = calloc(1, sizeof(struct info_source_version));
					if (FAILEDTOALLOC(v->next)) {
						free(version);
						result = RET_ERROR_OOM;
						break;
					}
					v = v->next;
					v->version = version;
					version = NULL;
					RET_UPDATE(result, RET_OK);
					break;
				}
				v = v->next;
			}
			free(version);
		}
		r = target_closeiterator(&target_cursor);
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

static retvalue process_binaries(struct database *db, struct distribution *d, struct info_source *sources, retvalue (*action)(struct distribution *, struct target *,const char *, const char *, const char *, const char *, void *), void *privdata) {
	struct target *t;
	struct target_cursor target_cursor = TARGET_CURSOR_ZERO;
	const char *name, *chunk;
	retvalue result = RET_NOTHING, r;

	for (t = d->targets ; t != NULL ; t = t->next) {
		if (t->architecture_atom == architecture_source)
			continue;
		r = target_openiterator(t, db, true, &target_cursor);
		if (RET_WAS_ERROR(r)) {
			RET_UPDATE(result, r);
			break;
		}
		while (target_nextpackage(&target_cursor, &name, &chunk)) {
			char *source, *version;
			struct info_source *s;
			struct info_source_version *v;

			r = t->getsourceandversion(chunk, name,
					&source, &version);
			if (!RET_IS_OK(r)) {
				RET_UPDATE(result, r);
				continue;
			}
			s = sources;
			while (s != NULL && strcmp(source, s->name) < 0) {
				s = s->next;
			}
			if (strcmp(source, s->name) == 0) {
				v = &s->version;
				while (v != NULL && strcmp(version, v->version) != 0)
					v = v->next;
			} else
				v = NULL;
			if (v != NULL) {
				v->used = true;
			} else if (action != NULL) {
				r = action(d, t,
						name, source, version, chunk,
						privdata);
				RET_UPDATE(result, r);
			}
			free(source); free(version);
		}
		r = target_closeiterator(&target_cursor);
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

	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		if( pkg->refcounts[i] == 0 )
			continue;
		if( pkg->filetypes[i] == 's' )
			hassource = true;
		if( pkg->filetypes[i] == 'b' )
			hasbinary = true;
		if( pkg->filetypes[i] == 'a' )
			hasbinary = true;
	}
	if( hassource && ! hasbinary ) {
		printf("%s %s %s\n", d->codename, pkg->sourcename, pkg->sourceversion);
		return RET_OK;
	}
	return RET_NOTHING;
}

retvalue unusedsources(struct database *database, struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;
		if( !atomlist_in(&d->architectures, architecture_source) )
			continue;
		if( d->tracking != dt_NONE ) {
			r = tracking_foreach_ro(database, d, listunusedsources);
			RET_UPDATE(result, r);
			if( RET_WAS_ERROR(r) )
				return r;
			continue;
		}
		struct info_source *sources = NULL;
		const struct info_source *s;
		const struct info_source_version *v;

		r = collect_source_versions(database, d, &sources);
		if (!RET_IS_OK(r))
			continue;

		r = process_binaries(database, d, sources, NULL, NULL);
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

	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		if( pkg->refcounts[i] == 0 )
			continue;
		if( pkg->filetypes[i] == 's' )
			hassource = true;
		if( pkg->filetypes[i] == 'b' )
			hasbinary = true;
		if( pkg->filetypes[i] == 'a' )
			hasbinary = true;
	}
	if( hasbinary && ! hassource ) {
		for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
			if( pkg->refcounts[i] == 0 )
				continue;
			if( pkg->filetypes[i] != 'b' && pkg->filetypes[i] != 'a' )
				continue;
			printf("%s %s %s %s\n", d->codename, pkg->sourcename, pkg->sourceversion, pkg->filekeys.values[i]);
		}
		return RET_OK;
	}
	return RET_NOTHING;
}

static retvalue listmissing(struct distribution *d, struct target *t, UNUSED(const char *name), const char *source, const char *version, const char *chunk, UNUSED(void*data)) {
	retvalue r;
	struct strlist list;

	r = t->getfilekeys(chunk, &list);
	if (!RET_IS_OK(r))
		return r;
	assert (list.count == 1);
	printf("%s %s %s %s\n", d->codename, source, version, list.values[0]);
	strlist_done(&list);
	return RET_OK;
}

retvalue sourcemissing(struct database *database, struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;
		if( !atomlist_in(&d->architectures, architecture_source) ) {
			if( verbose >= 0 )
				fprintf(stderr,
"Not processing distribution '%s', as it has no source packages.\n",
						d->codename);
			continue;
		}
		if( d->tracking != dt_NONE ) {
			r = tracking_foreach_ro(database, d, listsourcemissing);
			RET_UPDATE(result, r);
			if( RET_WAS_ERROR(r) )
				return r;
		} else {
			struct info_source *sources = NULL;

			r = collect_source_versions(database, d, &sources);
			if (!RET_IS_OK(r))
				continue;

			r = process_binaries(database, d, sources,
					listmissing, NULL);
			RET_UPDATE(result, r);
			free_source_info(sources);
		}

	}
	return result;
}

static retvalue listcruft(struct distribution *d, const struct trackedpackage *pkg) {
	bool hasbinary = false, hassource = false;
	int i;

	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		if( pkg->refcounts[i] == 0 )
			continue;
		if( pkg->filetypes[i] == 's' )
			hassource = true;
		if( pkg->filetypes[i] == 'b' )
			hasbinary = true;
		if( pkg->filetypes[i] == 'a' )
			hasbinary = true;
	}
	if( hasbinary && ! hassource ) {
		printf("binaries-without-source %s %s %s\n", d->codename, pkg->sourcename, pkg->sourceversion);
		return RET_OK;
	} else if( hassource && ! hasbinary ) {
		printf("source-without-binaries %s %s %s\n", d->codename, pkg->sourcename, pkg->sourceversion);
		return RET_OK;
	}
	return RET_NOTHING;
}

static retvalue listmissingonce(struct distribution *d, UNUSED(struct target *t), UNUSED(const char *name), const char *source, const char *version, UNUSED(const char *chunk), void *data) {
	struct info_source **already = data;
	struct info_source *s;

	for (s = *already ; s != NULL ; s = s->next) {
		if (strcmp(s->name, source) != 0)
			continue;
		if (strcmp(s->version.version, version) != 0)
			continue;
		return RET_NOTHING;
	}
	s = calloc(1, sizeof(struct info_source));
	if (FAILEDTOALLOC(s))
		return RET_ERROR_OOM;
	s->name = strdup(source);
	s->version.version = strdup(version);
	if (FAILEDTOALLOC(s->name) || FAILEDTOALLOC(s->version.version)) {
		free(s->name);
		free(s->version.version);
		free(s);
		return RET_ERROR_OOM;
	}
	s->next = *already;
	*already = s;
	printf("binaries-without-source %s %s %s\n",
			d->codename, source, version);
	return RET_OK;
}

retvalue reportcruft(struct database *database, struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;
		if( !atomlist_in(&d->architectures, architecture_source) ) {
			if( verbose >= 0 )
				fprintf(stderr,
"Not processing distribution '%s', as it has no source packages.\n",
						d->codename);
			continue;
		}
		if( d->tracking != dt_NONE ) {
			r = tracking_foreach_ro(database, d, listcruft);
			RET_UPDATE(result, r);
			if( RET_WAS_ERROR(r) )
				return r;
			continue;
		}
		struct info_source *sources = NULL;
		struct info_source *list = NULL;
		const struct info_source *s;
		const struct info_source_version *v;

		r = collect_source_versions(database, d, &sources);
		if (!RET_IS_OK(r))
			continue;

		r = process_binaries(database, d, sources,
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
