/*  This file is part of "reprepro"
 *  Copyright (C) 2005,2006,2007,2008,2009,2016 Bernhard R. Link
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

#include <assert.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "error.h"
#include "names.h"
#include "dirs.h"
#include "names.h"
#include "reference.h"
#include "ignore.h"
#include "configparser.h"
#include "package.h"

#include "database_p.h"
#include "tracking.h"

#ifndef NOPARANOIA
#define PARANOIA
#endif

struct s_tracking {
	char *codename;
	struct table *table;
	enum trackingtype type;
	struct trackingoptions options;
};

retvalue tracking_done(trackingdb db) {
	retvalue r;

	if (db == NULL)
		return RET_OK;

	r = table_close(db->table);
	free(db->codename);
	free(db);
	return r;
}

retvalue tracking_initialize(/*@out@*/trackingdb *db, const struct distribution *distribution, bool readonly) {
	struct s_tracking *t;
	retvalue r;

	t = zNEW(struct s_tracking);
	if (FAILEDTOALLOC(t))
		return RET_ERROR_OOM;
	t->codename = strdup(distribution->codename);
	if (FAILEDTOALLOC(t->codename)) {
		free(t);
		return RET_ERROR_OOM;
	}
	assert (distribution->tracking != dt_NONE || readonly);
	t->type = distribution->tracking;
	t->options = distribution->trackingoptions;
	r = database_opentracking(t->codename, readonly, &t->table);
	if (!RET_IS_OK(r)) {
		free(t->codename);
		free(t);
		return r;
	}
	*db = t;
	return RET_OK;
}

static inline enum filetype filetypechar(enum filetype filetype) {
	switch (filetype) {
		case ft_LOG:
		case ft_BUILDINFO:
		case ft_CHANGES:
		case ft_ALL_BINARY:
		case ft_ARCH_BINARY:
		case ft_SOURCE:
		case ft_XTRA_DATA:
			return filetype;
	}
	assert(false);
	return ft_XTRA_DATA;
}

retvalue trackedpackage_addfilekey(trackingdb tracks, struct trackedpackage *pkg, enum filetype filetype, char *filekey, bool used) {
	char *id;
	enum filetype ft = filetypechar(filetype);
	int i, *newrefcounts;
	enum filetype *newfiletypes;
	retvalue r;

	if (FAILEDTOALLOC(filekey))
		return RET_ERROR_OOM;

	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		if (strcmp(pkg->filekeys.values[i], filekey) == 0) {
			if (pkg->filetypes[i] != ft) {
				/* if old file has refcount 0, just repair: */
				if (pkg->refcounts[i] <= 0) {
					free(filekey);
					pkg->filetypes[i] = ft;
					if (used)
						pkg->refcounts[i] = 1;
					return RET_OK;
				}
				fprintf(stderr,
"Filekey '%s' already registered for '%s_%s' as type '%c' is tried to be reregistered as type '%c'!\n",
						filekey, pkg->sourcename,
						pkg->sourceversion,
						pkg->filetypes[i], ft);
				free(filekey);
				return RET_ERROR;
			}
			free(filekey);
			if (used)
				pkg->refcounts[i]++;
			return RET_OK;
		}
	}

	newrefcounts = realloc(pkg->refcounts,
			(pkg->filekeys.count + 1) * sizeof(int));
	if (FAILEDTOALLOC(newrefcounts)) {
		free(filekey);
		return RET_ERROR_OOM;
	}
	if (used)
		newrefcounts[pkg->filekeys.count]=1;
	else
		newrefcounts[pkg->filekeys.count]=0;
	pkg->refcounts = newrefcounts;
	newfiletypes = realloc(pkg->filetypes,
			(pkg->filekeys.count + 1) * sizeof(enum filetype));
	if (FAILEDTOALLOC(newfiletypes)) {
		free(filekey);
		return RET_ERROR_OOM;
	}
	newfiletypes[pkg->filekeys.count] = filetype;
	pkg->filetypes = newfiletypes;

	r = strlist_add(&pkg->filekeys, filekey);
	if (RET_WAS_ERROR(r))
		return r;

	id = calc_trackreferee(tracks->codename,
			pkg->sourcename, pkg->sourceversion);
	if (FAILEDTOALLOC(id))
		return RET_ERROR_OOM;
	r = references_increment(filekey, id);
	free(id);
	return r;
}

retvalue trackedpackage_adddupfilekeys(trackingdb tracks, struct trackedpackage *pkg, enum filetype filetype, const struct strlist *filekeys, bool used) {
	int i;
	retvalue result, r;
	assert (filekeys != NULL);

	result = RET_OK;
	for (i = 0 ; i < filekeys->count ; i++) {
		char *filekey = strdup(filekeys->values[i]);
		r = trackedpackage_addfilekey(tracks, pkg, filetype,
				filekey, used);
		RET_UPDATE(result, r);
	}
	return result;
}

static inline retvalue trackedpackage_removefilekey(trackingdb tracks, struct trackedpackage *pkg, const char *filekey) {
	int i;

	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		if (strcmp(pkg->filekeys.values[i], filekey) == 0) {
			if (pkg->refcounts[i] > 0) {
				pkg->refcounts[i]--;
			} else
				fprintf(stderr,
"Warning: tracking database of %s has inconsistent refcounts of %s_%s.\n",
						tracks->codename,
						pkg->sourcename,
						pkg->sourceversion);

			return RET_OK;
		}
	}
	fprintf(stderr,
"Warning: tracking database of %s missed files for %s_%s.\n",
		tracks->codename, pkg->sourcename, pkg->sourceversion);
	return RET_OK;

}

retvalue trackedpackage_removefilekeys(trackingdb tracks, struct trackedpackage *pkg, const struct strlist *filekeys) {
	int i;
	retvalue result, r;
	assert (filekeys != NULL);

	result = RET_OK;
	for (i = 0 ; i < filekeys->count ; i++) {
		const char *filekey = filekeys->values[i];
		r = trackedpackage_removefilekey(tracks, pkg, filekey);
		RET_UPDATE(result, r);
	}
	return result;
}

void trackedpackage_free(struct trackedpackage *pkg) {
	if (pkg != NULL) {
		free(pkg->sourcename);
		free(pkg->sourceversion);
		strlist_done(&pkg->filekeys);
		free(pkg->refcounts);
		free(pkg->filetypes);
		free(pkg);
	}
}

static inline int parsenumber(const char **d, size_t *s) {
	int count;

	count = 0;
	do {
		if (**d < '0' || **d > '7')
			return -1;
		count = (count*8) + (**d-'0');
		(*d)++;
		(*s)--;
		if (*s == 0)
			return -1;
	} while (**d != '\0');
	(*d)++;
	(*s)--;
	return count;
}

static retvalue tracking_new(const char *sourcename, const char *version, /*@out@*/struct trackedpackage **pkg) {
	struct trackedpackage *p;
	assert (pkg != NULL && sourcename != NULL && version != NULL);

	p = zNEW(struct trackedpackage);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	p->sourcename = strdup(sourcename);
	p->sourceversion = strdup(version);
	p->flags.isnew = true;
	if (FAILEDTOALLOC(p->sourcename) || FAILEDTOALLOC(p->sourceversion)) {
		trackedpackage_free(p);
		return RET_ERROR_OOM;
	}
	*pkg = p;
	return RET_OK;
}

static inline retvalue parse_data(const char *name, const char *version, const char *data, size_t datalen, /*@out@*/struct trackedpackage **pkg) {
	struct trackedpackage *p;
	int i;

	p = zNEW(struct trackedpackage);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	p->sourcename = strdup(name);
	p->sourceversion = strdup(version);
	if (FAILEDTOALLOC(p->sourcename)
			|| FAILEDTOALLOC(p->sourceversion)
		/*	|| FAILEDTOALLOC(p->sourcedir) */) {
		trackedpackage_free(p);
		return RET_ERROR_OOM;
	}
	while (datalen > 0 && *data != '\0') {
		char *filekey;
		const char *separator;
		size_t filekeylen;
		retvalue r;

		if (((p->filekeys.count)&31) == 0) {
			enum filetype *n = realloc(p->filetypes,
				(p->filekeys.count+32)*sizeof(enum filetype));
			if (FAILEDTOALLOC(n)) {
				trackedpackage_free(p);
				return RET_ERROR_OOM;
			}
			p->filetypes = n;
		}
		p->filetypes[p->filekeys.count] = *data;
		data++; datalen--;
		separator = memchr(data, '\0', datalen);
		if (separator == NULL) {
			fprintf(stderr,
"Internal Error: Corrupt tracking data for %s %s\n",
					name, version);
			trackedpackage_free(p);
			return RET_ERROR;
		}
		filekeylen = separator - data;
		filekey = strndup(data, filekeylen);
		if (FAILEDTOALLOC(filekey)) {
			trackedpackage_free(p);
			return RET_ERROR_OOM;
		}
		r = strlist_add(&p->filekeys, filekey);
		if (RET_WAS_ERROR(r)) {
			trackedpackage_free(p);
			return r;
		}
		data += filekeylen + 1;
		datalen -= filekeylen + 1;
	}
	data++; datalen--;
	p->refcounts = nzNEW(p->filekeys.count, int);
	if (FAILEDTOALLOC(p->refcounts)) {
		trackedpackage_free(p);
		return RET_ERROR_OOM;
	}
	for (i = 0 ; i < p->filekeys.count ; i++) {
		if ((p->refcounts[i] = parsenumber(&data, &datalen)) < 0) {
			fprintf(stderr,
"Internal Error: Corrupt tracking data for %s %s\n",
					name, version);
			trackedpackage_free(p);
			return RET_ERROR;
		}
	}
	if (datalen > 0) {
		fprintf(stderr,
"Internal Error: Trailing garbage in tracking data for %s %s\n (%ld bytes)",
					name, version, (long)datalen);
		trackedpackage_free(p);
		return RET_ERROR;
	}
	p->flags.isnew = false;
	p->flags.deleted = false;
	*pkg = p;
	return RET_OK;
}

retvalue tracking_get(trackingdb t, const char *sourcename, const char *version, /*@out@*/struct trackedpackage **pkg) {
	const char *data;
	size_t datalen;
	retvalue r;

	assert (pkg != NULL && sourcename != NULL && version != NULL);

	r = table_getpair(t->table, sourcename, version, &data, &datalen);
	if (!RET_IS_OK(r))
		return r;
	return parse_data(sourcename, version, data, datalen, pkg);
}

retvalue tracking_getornew(trackingdb tracks, const char *name, const char *version, /*@out@*/struct trackedpackage **pkg) {
	retvalue r;
	r = tracking_get(tracks, name, version, pkg);
	if (r == RET_NOTHING)
		r = tracking_new(name, version, pkg);
	return r;
}

static retvalue gen_data(struct trackedpackage *pkg, /*@out@*/char **newdata_p, /*@out@*/size_t *newdatalen_p) {
	size_t versionsize = strlen(pkg->sourceversion)+1;
	int i;
	char *d, *data;
	size_t datalen;

	datalen = versionsize + 1;
	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		size_t l;
		l = strlen(pkg->filekeys.values[i]);
		if (l > 0)
			datalen += l+9;
	}
	data = malloc(datalen + 1);
	if (FAILEDTOALLOC(data))
		return RET_ERROR_OOM;
	memcpy(data, pkg->sourceversion, versionsize);
	d = data + versionsize;
	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		size_t l;
		l = strlen(pkg->filekeys.values[i]);
		if (l > 0) {
			*d = pkg->filetypes[i];
			d++;
			memcpy(d, pkg->filekeys.values[i], l + 1);
			d+=l+1;
		}
	}
	*d ='\0'; d++;
	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		int j;
#define MAXREFCOUNTOCTETS 7
		char countstring[MAXREFCOUNTOCTETS];
		size_t count = pkg->refcounts[i];

		countstring[MAXREFCOUNTOCTETS-1] = '\0';
		for (j = MAXREFCOUNTOCTETS-2 ; j >= 0 ; j--) {
			countstring[j] = '0' + (count & 7);
			count >>= 3;
			if (count == 0)
				break;
		}
#undef MAXREFCOUNTOCTETS
		assert (count == 0);

		memcpy(d, countstring+j, 7 - j);
		d+=7-j;
		datalen -= j;
	}
	*d ='\0';
	assert ((size_t)(d-data) == datalen);
	*newdata_p = data;
	*newdatalen_p = datalen;
	return RET_OK;
}

static retvalue tracking_saveatcursor(trackingdb t, struct cursor *cursor, struct trackedpackage *pkg) {
	if (pkg->flags.deleted) {
		/* delete if delete is requested
		 * (all unreferencing has to be done before) */
		return cursor_delete(t->table, cursor,
				pkg->sourcename, pkg->sourceversion);
	} else {
		char *newdata;
		size_t newdatalen;
		retvalue r;

		r = gen_data(pkg, &newdata, &newdatalen);
		if (RET_IS_OK(r)) {
			r = cursor_replace(t->table, cursor,
					newdata, newdatalen);
			free(newdata);
		}
		return r;
	}
}

static retvalue tracking_saveonly(trackingdb t, struct trackedpackage *pkg) {
	retvalue r, r2;
	char *newdata;
	size_t newdatalen;

	assert (pkg != NULL);

	if (!pkg->flags.isnew) {
		struct cursor *cursor;

		r = table_newpairedcursor(t->table,
				pkg->sourcename, pkg->sourceversion, &cursor,
				NULL, NULL);
		if (RET_WAS_ERROR(r))
			return r;
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Internal error: tracking_save with isnew=false called but could not find %s_%s in %s!\n",
					pkg->sourcename, pkg->sourceversion,
					t->codename);
			pkg->flags.isnew = true;
		} else {
			r = tracking_saveatcursor(t, cursor, pkg);
			r2 = cursor_close(t->table, cursor);
			RET_ENDUPDATE(r, r2);
			return r;
		}
	}

	if (pkg->flags.deleted)
		return RET_OK;

	r = gen_data(pkg, &newdata, &newdatalen);
	assert (r != RET_NOTHING);
	if (!RET_IS_OK(r))
		return r;

	r = table_addrecord(t->table, pkg->sourcename, newdata, newdatalen, false);
	free(newdata);
	if (verbose > 18)
		fprintf(stderr, "Adding tracked package '%s'_'%s' to '%s'\n",
				pkg->sourcename, pkg->sourceversion,
				t->codename);
	return r;
}

retvalue tracking_save(trackingdb t, struct trackedpackage *pkg) {
	retvalue r = tracking_saveonly(t, pkg);
	trackedpackage_free(pkg);
	return r;
}

retvalue tracking_listdistributions(struct strlist *distributions) {
	return database_listsubtables("tracking.db", distributions);
}

retvalue tracking_drop(const char *codename) {
	retvalue result, r;

	result = database_dropsubtable("tracking.db", codename);
	r = references_remove(codename);
	RET_UPDATE(result, r);

	return result;
}

static retvalue tracking_recreatereferences(trackingdb t) {
	struct cursor *cursor;
	retvalue result, r;
	struct trackedpackage *pkg;
	char *id;
	int i;
	const char *key, *value, *data;
	size_t datalen;

	r = table_newglobalcursor(t->table, &cursor);
	if (!RET_IS_OK(r))
		return r;

	result = RET_NOTHING;

	while (cursor_nextpair(t->table, cursor,
				&key, &value, &data, &datalen)) {
		r = parse_data(key, value, data, datalen, &pkg);
		if (RET_WAS_ERROR(r)) {
			(void)cursor_close(t->table, cursor);
			return r;
		}
		id = calc_trackreferee(t->codename, pkg->sourcename,
				                    pkg->sourceversion);
		if (FAILEDTOALLOC(id)) {
			trackedpackage_free(pkg);
			(void)cursor_close(t->table, cursor);
			return RET_ERROR_OOM;
		}
		for (i = 0 ; i < pkg->filekeys.count ; i++) {
			const char *filekey = pkg->filekeys.values[i];
			r = references_increment(filekey, id);
			RET_UPDATE(result, r);
		}
		free(id);
		trackedpackage_free(pkg);
	}
	r = cursor_close(t->table, cursor);
	RET_UPDATE(result, r);
	return result;
}

retvalue tracking_rereference(struct distribution *distribution) {
	retvalue result, r;
	trackingdb tracks;

	result = references_remove(distribution->codename);
	if (distribution->tracking == dt_NONE)
		return result;
	r = tracking_initialize(&tracks, distribution, true);
	RET_UPDATE(result, r);
	if (!RET_IS_OK(r))
		return result;
	r = tracking_recreatereferences(tracks);
	RET_UPDATE(result, r);
	r = tracking_done(tracks);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue tracking_remove(trackingdb t, const char *sourcename, const char *version) {
	retvalue result, r;
	struct cursor *cursor;
	const char *data;
	size_t datalen;
	char *id;
	struct trackedpackage *pkg SETBUTNOTUSED(= NULL);

	r = table_newpairedcursor(t->table, sourcename, version, &cursor,
			&data, &datalen);
	if (!RET_IS_OK(r))
		return r;

	id = calc_trackreferee(t->codename, sourcename, version);
	if (FAILEDTOALLOC(id)) {
		(void)cursor_close(t->table, cursor);
		return RET_ERROR_OOM;
	}

	result = parse_data(sourcename, version, data, datalen, &pkg);
	if (RET_IS_OK(r)) {
		assert (pkg != NULL);
		r = references_delete(id, &pkg->filekeys, NULL);
		RET_UPDATE(result, r);
		trackedpackage_free(pkg);
	} else {
		RET_UPDATE(result, r);
		fprintf(stderr,
"Could not parse data, removing all references blindly...\n");
		r = references_remove(id);
		RET_UPDATE(result, r);
	}
	free(id);
	r = cursor_delete(t->table, cursor, sourcename, version);
	if (RET_IS_OK(r))
		fprintf(stderr, "Removed %s_%s from %s.\n",
				sourcename, version, t->codename);
	RET_UPDATE(result, r);
	r = cursor_close(t->table, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

static void print(const char *codename, const struct trackedpackage *pkg){
	int i;

	printf("Distribution: %s\n", codename);
	printf("Source: %s\n", pkg->sourcename);
	printf("Version: %s\n", pkg->sourceversion);
	printf("Files:\n");
	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		const char *filekey = pkg->filekeys.values[i];

		printf(" %s %c %d\n", filekey,
				pkg->filetypes[i], pkg->refcounts[i]);
	}
	(void)fputs("\n", stdout);
}

retvalue tracking_printall(trackingdb t) {
	struct cursor *cursor;
	retvalue result, r;
	struct trackedpackage *pkg;
	const char *key, *value, *data;
	size_t datalen;

	r = table_newglobalcursor(t->table, &cursor);
	if (!RET_IS_OK(r))
		return r;

	result = RET_NOTHING;

	while (cursor_nextpair(t->table, cursor,
				&key, &value, &data, &datalen)) {
		r = parse_data(key, value, data, datalen, &pkg);
		if (RET_IS_OK(r)) {
			print(t->codename, pkg);
			trackedpackage_free(pkg);
		}
		RET_UPDATE(result, r);
	}
	r = cursor_close(t->table, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue tracking_foreach_ro(struct distribution *d, tracking_foreach_ro_action *action) {
	trackingdb t;
	struct cursor *cursor;
	retvalue result, r;
	struct trackedpackage *pkg;
	const char *key, *value, *data;
	size_t datalen;

	r = tracking_initialize(&t, d, true);
	if (!RET_IS_OK(r))
		return r;

	r = table_newglobalcursor(t->table, &cursor);
	if (!RET_IS_OK(r)) {
		(void)tracking_done(t);
		return r;
	}

	result = RET_NOTHING;
	while (cursor_nextpair(t->table, cursor,
				&key, &value, &data, &datalen)) {
		r = parse_data(key, value, data, datalen, &pkg);
		if (RET_IS_OK(r)) {
			r = action(d, pkg);
			trackedpackage_free(pkg);
		}
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}
	r = cursor_close(t->table, cursor);
	RET_ENDUPDATE(result, r);
	r = tracking_done(t);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue tracking_parse(struct distribution *d, struct configiterator *iter) {
	enum trackingflags { tf_keep, tf_all, tf_minimal,
		tf_includechanges, tf_includebyhand, tf_includelogs,
		tf_includebuildinfos,
		tf_keepsources,
		tf_needsources, tf_embargoalls,
		tf_COUNT /* must be last */
	};
	static const struct constant trackingflags[] = {
		{"keep",	tf_keep},
		{"all",		tf_all},
		{"minimal",	tf_minimal},
		{"includechanges",	tf_includechanges},
		{"includebuildinfos",	tf_includebuildinfos},
		{"includelogs",		tf_includelogs},
		{"includebyhand",	tf_includebyhand},
		{"keepsources",		tf_keepsources},
		{"needsources",		tf_needsources},
		{"embargoalls",		tf_embargoalls},
		{NULL,		-1}
	};
	bool flags[tf_COUNT];
	retvalue r;
	int modecount;

	assert (d->tracking == dt_NONE);
	memset(flags, 0, sizeof(flags));
	r = config_getflags(iter, "Tracking", trackingflags, flags,
			IGNORABLE(unknownfield), "");
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	modecount = flags[tf_keep]?1:0 + flags[tf_minimal]?1:0 + flags[tf_all]?1:0;
	if (modecount > 1) {
		fprintf(stderr,
"Error parsing config file %s, line %u:\n"
"Only one of 'keep','all' or 'minimal' can be in one Tracking header.\n",
			config_filename(iter), config_line(iter));
		return RET_ERROR;
	}
	if (modecount < 1) {
		fprintf(stderr,
"Error parsing config file %s, line %u, column %u:\n"
"Tracking mode ('keep','all' or 'minimal') expected.\n",
			config_filename(iter), config_line(iter),
			config_column(iter));
		return RET_ERROR;
	}
	if (flags[tf_keep])
		d->tracking = dt_KEEP;
	else if (flags[tf_minimal])
		d->tracking = dt_MINIMAL;
	else
		d->tracking = dt_ALL;

	d->trackingoptions.includechanges = flags[tf_includechanges];
	d->trackingoptions.includebyhand = flags[tf_includebyhand];
	d->trackingoptions.includebuildinfos = flags[tf_includebuildinfos];
	d->trackingoptions.includelogs = flags[tf_includelogs];
	d->trackingoptions.keepsources = flags[tf_keepsources];
	d->trackingoptions.needsources = flags[tf_needsources];
	if (flags[tf_needsources])
		fprintf(stderr,
"Warning parsing config file %s, line %u:\n"
"'needsources' ignored as not yet supported.\n",
			config_filename(iter), config_line(iter));
	d->trackingoptions.embargoalls = flags[tf_embargoalls];
	if (flags[tf_embargoalls])
		fprintf(stderr,
"Warning parsing config file %s, line %u:\n"
"'embargoall' ignored as not yet supported.\n",
			config_filename(iter), config_line(iter));
	return RET_OK;
}

static retvalue trackingdata_remember(struct trackingdata *td, const char*name, const char*version) {
	struct trackingdata_remember *r;

	r = NEW(struct trackingdata_remember);
	if (FAILEDTOALLOC(r))
		return RET_ERROR_OOM;
	r->name = strdup(name);
	r->version = strdup(version);
	if (FAILEDTOALLOC(r->name) || FAILEDTOALLOC(r->version)) {
		free(r->name);
		free(r->version);
		free(r);
		return RET_ERROR_OOM;
	}
	r->next = td->remembered;
	td->remembered = r;
	return RET_OK;
}

retvalue trackingdata_summon(trackingdb tracks, const char *name, const char *version, struct trackingdata *data) {
	struct trackedpackage *pkg;
	retvalue r;

	r = tracking_getornew(tracks, name, version, &pkg);
	assert (r != RET_NOTHING);
	if (RET_IS_OK(r)) {
		data->tracks = tracks;
		data->pkg = pkg;
		data->remembered = NULL;
		return r;
	}
	return r;
}

retvalue trackingdata_new(trackingdb tracks, struct trackingdata *data) {

	data->tracks = tracks;
	data->pkg = NULL;
	data->remembered = NULL;
	return RET_OK;
}

retvalue trackingdata_switch(struct trackingdata *data, const char *source, const char *version) {
	retvalue r;

	if (data->pkg != NULL) {
		if (strcmp(data->pkg->sourcename, source) == 0 &&
				strcmp(data->pkg->sourceversion, version) == 0)
			return RET_OK;
		r = tracking_saveonly(data->tracks, data->pkg);
		if (RET_WAS_ERROR(r))
			return r;
		r = trackingdata_remember(data, data->pkg->sourcename,
				data->pkg->sourceversion);
		strlist_done(&data->pkg->filekeys);
		free(data->pkg->sourcename);
		free(data->pkg->sourceversion);
		free(data->pkg->refcounts);
		free(data->pkg->filetypes);
		free(data->pkg);
		data->pkg = NULL;
		if (RET_WAS_ERROR(r))
			return r;
	}
	r = tracking_getornew(data->tracks, source, version, &data->pkg);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}

retvalue trackingdata_insert(struct trackingdata *data, enum filetype filetype, const struct strlist *filekeys, /*@null@*/const struct package *old, /*@null@*/const struct strlist *oldfilekeys) {
	retvalue result, r;
	struct trackedpackage *pkg;

	assert (data != NULL);
	assert(data->pkg != NULL);
	result = trackedpackage_adddupfilekeys(data->tracks, data->pkg,
			filetype, filekeys, true);
	if (RET_WAS_ERROR(result)) {
		return result;
	}
	if (old == NULL || old->source == NULL || old->sourceversion == NULL
			|| oldfilekeys == NULL) {
		return RET_OK;
	}
	if (strcmp(old->sourceversion, data->pkg->sourceversion) == 0 &&
			strcmp(old->source, data->pkg->sourcename) == 0) {
		/* Unlikely, but it may also be the same source version as
		 * the package we are currently adding */
		return trackedpackage_removefilekeys(data->tracks, data->pkg,
				oldfilekeys);
	}
	r = tracking_get(data->tracks, old->source, old->sourceversion, &pkg);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Could not found tracking data for %s_%s in %s to remove old files from it.\n",
			old->source, old->sourceversion,
			data->tracks->codename);
		return result;
	}
	r = trackedpackage_removefilekeys(data->tracks, pkg, oldfilekeys);
	RET_UPDATE(result, r);
	r = tracking_save(data->tracks, pkg);
	RET_UPDATE(result, r);
	r = trackingdata_remember(data, old->source, old->sourceversion);
	RET_UPDATE(result, r);

	return result;
}

retvalue trackingdata_remove(struct trackingdata *data, const char* oldsource, const char*oldversion, const struct strlist *oldfilekeys) {
	retvalue result, r;
	struct trackedpackage *pkg;

	assert(oldsource != NULL && oldversion != NULL && oldfilekeys != NULL);
	if (data->pkg != NULL &&
			strcmp(oldversion, data->pkg->sourceversion) == 0 &&
			strcmp(oldsource, data->pkg->sourcename) == 0) {
		/* Unlikely, but it may also be the same source version as
		 * the package we are currently adding */
		return trackedpackage_removefilekeys(data->tracks,
				data->pkg, oldfilekeys);
	}
	result = tracking_get(data->tracks, oldsource, oldversion, &pkg);
	if (RET_WAS_ERROR(result)) {
		return result;
	}
	if (result == RET_NOTHING) {
		fprintf(stderr,
"Could not found tracking data for %s_%s in %s to remove old files from it.\n",
			oldsource, oldversion, data->tracks->codename);
		return RET_OK;
	}
	r = trackedpackage_removefilekeys(data->tracks, pkg, oldfilekeys);
	RET_UPDATE(result, r);
	r = tracking_save(data->tracks, pkg);
	RET_UPDATE(result, r);
	r = trackingdata_remember(data, oldsource, oldversion);
	RET_UPDATE(result, r);

	return result;
}

void trackingdata_done(struct trackingdata *d) {
	trackedpackage_free(d->pkg);
	d->pkg = NULL;
	d->tracks = NULL;
	while (d->remembered != NULL) {
		struct trackingdata_remember *h = d->remembered;
		d->remembered = h->next;
		free(h->name);
		free(h->version);
		free(h);
	}

}

static inline retvalue trackedpackage_removeall(trackingdb tracks, struct trackedpackage *pkg) {
	retvalue result = RET_OK, r;
	char *id;

//	printf("[trackedpackage_removeall %s %s %s]\n", tracks->codename, pkg->sourcename, pkg->sourceversion);
	id = calc_trackreferee(tracks->codename, pkg->sourcename,
			pkg->sourceversion);
	if (FAILEDTOALLOC(id))
		return RET_ERROR_OOM;

	pkg->flags.deleted = true;
	r = references_delete(id, &pkg->filekeys, NULL);
	RET_UPDATE(result, r);
	free(id);
	strlist_done(&pkg->filekeys);
	strlist_init(&pkg->filekeys);
	free(pkg->refcounts); pkg->refcounts = NULL;
	return result;
}

static inline bool tracking_needed(trackingdb tracks, struct trackedpackage *pkg, int ofs) {
	if (pkg->refcounts[ofs] > 0)
		return true;
	// TODO: add checks so that only .changes, .buildinfo and .log files
	// belonging to still existing binaries are kept in minimal mode
	if (pkg->filetypes[ofs] == ft_LOG && tracks->options.includelogs)
		return true;
	if (pkg->filetypes[ofs] == ft_BUILDINFO && tracks->options.includebuildinfos)
		return true;
	if (pkg->filetypes[ofs] == ft_CHANGES && tracks->options.includechanges)
		return true;
	if (pkg->filetypes[ofs] == ft_XTRA_DATA)
		return true;
	if (pkg->filetypes[ofs] == ft_SOURCE && tracks->options.keepsources)
		return true;
	return false;

}

static inline retvalue trackedpackage_removeunneeded(trackingdb tracks, struct trackedpackage *pkg) {
	retvalue result = RET_OK, r;
	char *id = NULL;
	int i, j, count;

	assert(tracks->type == dt_MINIMAL);

	count = pkg->filekeys.count;
	j = 0;
	for (i = 0 ; i < count ; i++) {
		if (tracking_needed(tracks, pkg, i)) {
			if (j < i) {
				pkg->filekeys.values[j] = pkg->filekeys.values[i];
				pkg->refcounts[j] = pkg->refcounts[i];
				pkg->filetypes[j] = pkg->filetypes[i];
			}
			j++;
		} else {
			char *filekey = pkg->filekeys.values[i];
			pkg->filekeys.values[i] = NULL;
			if (FAILEDTOALLOC(id)) {
				id = calc_trackreferee(tracks->codename,
					pkg->sourcename, pkg->sourceversion);
				if (id == NULL)
					result = RET_ERROR_OOM;
			}
			if (id != NULL) {
//				printf("[trackedpackage_removeunneeded %s %s %s: '%s']\n", tracks->codename, pkg->sourcename, pkg->sourceversion, filekey);
				r = references_decrement(filekey, id);
				RET_UPDATE(result, r);
			}
			free(filekey);
		}
	}
	assert (j <= pkg->filekeys.count);
	pkg->filekeys.count = j;
	free(id);
	return result;
}

static inline retvalue trackedpackage_tidy(trackingdb tracks, struct trackedpackage *pkg) {
	int i;

	if (tracks->type == dt_KEEP)
		return RET_OK;
	/* look if anything clings to this package */
	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		if (pkg->refcounts[i] > 0)
			break;
	}
	if (i >= pkg->filekeys.count)

		/* nothing left, remove it all */
		return trackedpackage_removeall(tracks, pkg);

	else if (tracks->type == dt_MINIMAL)

		/* remove all files no longer needed */
		return trackedpackage_removeunneeded(tracks, pkg);
	else
		return RET_OK;
}

retvalue trackingdata_finish(trackingdb tracks, struct trackingdata *d) {
	retvalue r;
	assert (d->tracks == tracks);
	if (d->pkg != NULL) {
		r = trackedpackage_tidy(tracks, d->pkg);
		r = tracking_save(tracks, d->pkg);
	} else
		r = RET_OK;
	d->pkg = NULL;
	/* call for all remembered actions... */
	while (d->remembered != NULL) {
		struct trackingdata_remember *h = d->remembered;
		struct trackedpackage *pkg;
		d->remembered = h->next;
		r = tracking_get(tracks, h->name, h->version, &pkg);
		free(h->name);
		free(h->version);
		free(h);
		if (RET_IS_OK(r)) {
			r = trackedpackage_tidy(tracks, pkg);
			r = tracking_save(tracks, pkg);
		}
	}
	d->tracks = NULL;
	return r;

}

retvalue tracking_tidyall(trackingdb t) {
	struct cursor *cursor;
	retvalue result, r;
	struct trackedpackage *pkg;
	const char *key, *value, *data;
	size_t datalen;

	r = table_newglobalcursor(t->table, &cursor);
	if (!RET_IS_OK(r))
		return r;

	result = RET_NOTHING;

	while (cursor_nextpair(t->table, cursor,
				&key, &value, &data, &datalen)) {
		r = parse_data(key, value, data, datalen, &pkg);
		if (RET_WAS_ERROR(r)) {
			result = r;
			break;
		}
		r = trackedpackage_tidy(t, pkg);
		RET_UPDATE(result, r);
		r = tracking_saveatcursor(t, cursor, pkg);
		RET_UPDATE(result, r);
		trackedpackage_free(pkg);
	}
	r = cursor_close(t->table, cursor);
	RET_UPDATE(result, r);
	return result;
}

retvalue tracking_reset(trackingdb t) {
	struct cursor *cursor;
	retvalue result, r;
	struct trackedpackage *pkg;
	const char *key, *value, *data;
	char *newdata;
	size_t datalen, newdatalen;
	int i;

	r = table_newglobalcursor(t->table, &cursor);
	if (!RET_IS_OK(r))
		return r;

	result = RET_NOTHING;

	while (cursor_nextpair(t->table, cursor,
				&key, &value, &data, &datalen)) {
		// this would perhaps be more stable if it just replaced
		// everything within the string just received...
		result = parse_data(key, value, data, datalen, &pkg);
		if (RET_WAS_ERROR(result))
			break;
		for (i = 0 ; i < pkg->filekeys.count ; i++) {
			pkg->refcounts[i] = 0;
		}
		result = gen_data(pkg, &newdata, &newdatalen);
		trackedpackage_free(pkg);
		if (RET_IS_OK(result))
			result = cursor_replace(t->table, cursor,
					newdata, newdatalen);
		free(newdata);
		if (RET_WAS_ERROR(result))
			break;
	}
	r = cursor_close(t->table, cursor);
	RET_UPDATE(result, r);
	return result;
}

static retvalue tracking_foreachversion(trackingdb t, struct distribution *distribution,  const char *sourcename, retvalue (action)(trackingdb t, struct trackedpackage *, struct distribution *)) {
	struct cursor *cursor;
	retvalue result, r;
	struct trackedpackage *pkg;
	const char *value, *data;
	size_t datalen;

	r = table_newduplicatecursor(t->table, sourcename, &cursor,
			&value, &data, &datalen);
	if (!RET_IS_OK(r))
		return r;

	result = RET_NOTHING;

	do {
		r = parse_data(sourcename, value, data, datalen, &pkg);
		if (RET_WAS_ERROR(r)) {
			result = r;
			break;
		}
		if (verbose > 10)
			printf("Processing track of '%s' version '%s'\n",
					pkg->sourcename, pkg->sourceversion);
		r = action(t, pkg, distribution);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r)) {
			(void)cursor_close(t->table, cursor);
			trackedpackage_free(pkg);
			return r;
		}
		r = trackedpackage_tidy(t, pkg);
		RET_ENDUPDATE(result, r);
		r = tracking_saveatcursor(t, cursor, pkg);
		RET_UPDATE(result, r);
		trackedpackage_free(pkg);
	} while (cursor_nextpair(t->table, cursor, NULL,
				&value, &data, &datalen));
	r = cursor_close(t->table, cursor);
	RET_UPDATE(result, r);
	return result;
}


static retvalue targetremovesourcepackage(trackingdb t, struct trackedpackage *pkg, struct distribution *distribution, struct target *target) {
	size_t component_len, arch_len;
	retvalue result, r;
	int i;
	const char *packagetype = atoms_packagetypes[target->packagetype];
	const char *architecture = atoms_architectures[target->architecture];
	const char *component = atoms_components[target->component];

	result = RET_NOTHING;

	component_len = strlen(component);
	arch_len = strlen(architecture);
	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		const char *s, *basefilename, *filekey = pkg->filekeys.values[i];
		char *packagename;
		struct package package;
		struct strlist filekeys;
		bool savedstaletracking;

		if (pkg->refcounts[i] <= 0)
			continue;
		if (strncmp(filekey, "pool/", 5) != 0)
			continue;
		if (strncmp(filekey+5, component,
					component_len) != 0)
			continue;
		if (filekey[5+component_len] != '/')
			continue;
		/* check this file could actuall be in this target */
		if (pkg->filetypes[i] == ft_ALL_BINARY) {
			if (target->packagetype == pt_dsc)
				continue;
			s = strrchr(filekey, '.');
			if (s == NULL)
				continue;
			if (strcmp(s+1, packagetype) != 0)
				continue;
		} else if (pkg->filetypes[i] == ft_SOURCE) {
			if (target->packagetype != pt_dsc)
				continue;
			s = strrchr(filekey, '.');
			if (s == NULL)
				continue;
			if (strcmp(s+1, "dsc") != 0)
				continue;
		} else if (pkg->filetypes[i] == ft_ARCH_BINARY) {
			if (target->packagetype == pt_dsc)
				continue;
			s = strrchr(filekey, '_');
			if (s == NULL)
				continue;
			s++;
			if (strncmp(s, architecture, arch_len) != 0
			    || s[arch_len] != '.'
			    || strcmp(s+arch_len+1, packagetype) != 0)
				continue;
		} else
			continue;
		/* get this package, check it has the right source and version,
		 * and if yes, remove... */
		basefilename = strrchr(filekey, '/');
		if (basefilename == NULL)
			basefilename = filekey;
		else
			basefilename++;
		s = strchr(basefilename, '_');
		packagename = strndup(basefilename, s - basefilename);
		if (FAILEDTOALLOC(packagename))
			return RET_ERROR_OOM;
		r = package_get(target, packagename, NULL, &package);
		if (RET_WAS_ERROR(r)) {
			free(packagename);
			return r;
		}
		if (r == RET_NOTHING) {
			if (pkg->filetypes[i] != ft_ALL_BINARY
			    && verbose >= -1) {
				fprintf(stderr,
"Warning: tracking data might be inconsistent:\n"
"cannot find '%s' in '%s', but '%s' should be there.\n",
						packagename, target->identifier,
						filekey);
			}
			free(packagename);
			continue;
		}
		// TODO: ugly
		package.pkgname = packagename;
		packagename = NULL;

		r = package_getsource(&package);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r)) {
			package_done(&package);
			return r;
		}
		if (strcmp(package.source, pkg->sourcename) != 0) {
			if (pkg->filetypes[i] != ft_ALL_BINARY
			    && verbose >= -1) {
				fprintf(stderr,
"Warning: tracking data might be inconsistent:\n"
"'%s' has '%s' of source '%s', but source '%s' contains '%s'.\n",
						target->identifier, package.name,
						package.source, pkg->sourcename,
						filekey);
			}
			package_done(&package);
			continue;
		}
		if (strcmp(package.sourceversion, pkg->sourceversion) != 0) {
			if (pkg->filetypes[i] != ft_ALL_BINARY
			    && verbose >= -1) {
				fprintf(stderr,
"Warning: tracking data might be inconsistent:\n"
"'%s' has '%s' of source version '%s', but version '%s' contains '%s'.\n",
						target->identifier, package.name,
						package.sourceversion,
						pkg->sourceversion,
						filekey);
			}
			package_done(&package);
			continue;
		}
		r = target->getfilekeys(package.control, &filekeys);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r)) {
			package_done(&package);
			return r;
		}

		/* we remove the tracking data outself, so this is not
		 * told to remove the tracking data, so it might mark things
		 * as stale, which we do not want.. */
		savedstaletracking = target->staletracking;
		r = package_remove(&package, distribution->logger, NULL);
		target->staletracking = savedstaletracking;
		package_done(&package);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r)) {
			strlist_done(&filekeys);
			return r;
		}
		trackedpackage_removefilekeys(t, pkg, &filekeys);
		strlist_done(&filekeys);
		result = RET_OK;
	}
	return result;
}

/* Try to remove all packages causing refcounts in this tracking record */
static retvalue removesourcepackage(trackingdb t, struct trackedpackage *pkg, struct distribution *distribution) {
	struct target *target;
	retvalue result, r;
	int i;

	result = RET_NOTHING;
	for (target = distribution->targets ; target != NULL ;
	                                      target = target->next) {
		r = target_initpackagesdb(target, READWRITE);
		RET_ENDUPDATE(result, r);
		if (RET_IS_OK(r)) {
			r = targetremovesourcepackage(t, pkg,
					distribution, target);
			RET_UPDATE(result, r);
			RET_UPDATE(distribution->status, r);
			r = target_closepackagesdb(target);
			RET_ENDUPDATE(result, r);
			RET_ENDUPDATE(distribution->status, r);
			if (RET_WAS_ERROR(result))
				return result;
		}
	}
	for (i = 0 ; i < pkg->filekeys.count ; i++) {
		const char *filekey = pkg->filekeys.values[i];

		if (pkg->refcounts[i] <= 0)
			continue;
		if (pkg->filetypes[i] != ft_ALL_BINARY &&
		    pkg->filetypes[i] != ft_SOURCE &&
		    pkg->filetypes[i] != ft_ARCH_BINARY)
			continue;
		fprintf(stderr,
"There was an inconsistency in the tracking data of '%s':\n"
"'%s' has refcount > 0, but was nowhere found.\n",
				distribution->codename,
				filekey);
		pkg->refcounts[i] = 0;
	}
	return result;
}

retvalue tracking_removepackages(trackingdb t, struct distribution *distribution, const char *sourcename, /*@null@*/const char *version) {
	struct trackedpackage *pkg;
	retvalue result, r;

	if (version == NULL)
		return tracking_foreachversion(t, distribution,
				sourcename, removesourcepackage);
	result = tracking_get(t, sourcename, version, &pkg);
	if (RET_IS_OK(result)) {
		result = removesourcepackage(t, pkg, distribution);
		if (RET_IS_OK(result)) {
			r = trackedpackage_tidy(t, pkg);
			RET_ENDUPDATE(result, r);
			r = tracking_save(t, pkg);
			RET_ENDUPDATE(result, r);
		} else
			trackedpackage_free(pkg);
	}
	return result;
}

static retvalue package_retrack(struct package *package, void *data) {
	trackingdb tracks = data;

	return package->target->doretrack(package->name,
			package->control, tracks);
}

retvalue tracking_retrack(struct distribution *d, bool needsretrack) {
	struct target *t;
	trackingdb tracks;
	retvalue r, rr;

	if (d->tracking == dt_NONE)
		return RET_NOTHING;

	for (t = d->targets ; !needsretrack && t != NULL ; t = t->next) {
		if (t->staletracking)
			needsretrack = true;
	}
	if (!needsretrack)
		return RET_NOTHING;

	if (verbose > 0)
		printf("Retracking %s...\n", d->codename);

	r = tracking_initialize(&tracks, d, false);
	if (!RET_IS_OK(r))
		return r;
	/* first forget that any package is there*/
	r = tracking_reset(tracks);
	if (!RET_WAS_ERROR(r)) {
		/* add back information about actually used files */
		r = package_foreach(d,
				atom_unknown, atom_unknown, atom_unknown,
				package_retrack, NULL, tracks);
	}
	if (RET_IS_OK(r)) {
		for (t = d->targets ; t != NULL ; t = t->next) {
			t->staletracking = false;
		}
	}
	if (!RET_WAS_ERROR(r)) {
		/* now remove everything no longer needed */
		r = tracking_tidyall(tracks);
	}
	rr = tracking_done(tracks);
	RET_ENDUPDATE(r, rr);
	return r;
}
