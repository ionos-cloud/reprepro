/*  This file is part of "reprepro"
 *  Copyright (C) 2011 Bernhard R. Link
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
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "error.h"
#include "strlist.h"
#include "distribution.h"
#include "database.h"
#include "database_p.h"
#include "files.h"
#include "sizes.h"

struct distribution_sizes {
	struct distribution_sizes *next;
	const char *codename;
	char *v;
	size_t codename_len;
	struct {
		unsigned long long all, onlyhere;
	} this, withsnapshots;
	bool seen, seensnapshot;
};

static void distribution_sizes_freelist(struct distribution_sizes *ds) {
	while (ds != NULL) {
		struct distribution_sizes *s = ds;
		ds = ds->next;

		free(s->v);
		free(s);
	}
}

static bool fromdist(struct distribution_sizes *dist, const char *data, size_t len, bool *snapshot_p) {
	if (len < dist->codename_len + 1)
		return false;
	if (data[dist->codename_len] == '=')
		*snapshot_p = true;
	else if (data[dist->codename_len] == '|' ||
			data[dist->codename_len] == ' ')
		*snapshot_p = false;
	else
		return false;
	return memcmp(data, dist->codename, dist->codename_len) == 0;
}

static retvalue count_sizes(struct cursor *cursor, bool specific, struct distribution_sizes *ds, unsigned long long *all_p, unsigned long long *onlyall_p) {
	const char *key, *data;
	size_t len;
	char *last_file = NULL;
	unsigned long long filesize = 0;
	bool onlyone = true;
	struct distribution_sizes *last_dist;
	struct distribution_sizes *s;
	bool snapshot;
	unsigned long long all = 0, onlyall = 0;

	while (cursor_nexttempdata(rdb_references, cursor,
				&key, &data, &len)) {
		if (last_file == NULL || strcmp(last_file, key) != 0) {
			if (last_file != NULL) {
				free(last_file);
				for (s = ds ; s != NULL ; s = s->next) {
					s->seen = false;
					s->seensnapshot = false;
				}
			}
			last_file = strdup(key);
			if (FAILEDTOALLOC(last_file))
				return RET_ERROR_OOM;
			onlyone = true;
			filesize = 0;
			last_dist = NULL;
		}
		if (data[0] == 'u' && data[1] == '|') {
			data += 2;
			len -= 2;
		} else if (data[0] == 's' && data[1] == '=') {
			data += 2;
			len -= 2;
		}
		if (last_dist != NULL &&
				fromdist(last_dist, data, len, &snapshot)) {
			/* same distribution again */
			if (!snapshot && !last_dist->seen) {
				last_dist->seen = true;
				last_dist->this.all += filesize;
				if (onlyone)
					last_dist->this.onlyhere += filesize;
			}
			continue;
		}
		s = ds;
		while (s != NULL && !fromdist(s, data, len, &snapshot))
		     s = s->next;
		if (s == NULL) {
			if (onlyone && last_dist != NULL) {
				if (!last_dist->seen)
					last_dist->this.onlyhere -= filesize;
				last_dist->withsnapshots.onlyhere -= filesize;
			}
			if (last_dist != NULL)
				onlyall -= filesize;
			onlyone = false;
			if (!specific) {
				struct distribution_sizes **s_p = &ds->next;
				const char *p;

				p = data;
				while (*p != '\0' && *p != ' ' && *p != '|'
						&& *p != '=')
					p++;
				if (*p == '\0')
					continue;
				while (*s_p != NULL)
					s_p = &(*s_p)->next;
				s = zNEW(struct distribution_sizes);
				if (FAILEDTOALLOC(s)) {
					free(last_file);
					return RET_ERROR_OOM;
				}
				*s_p = s;
				s->v = strndup(data, (p-data) + 1);
				if (FAILEDTOALLOC(s)) {
					free(last_file);
					return RET_ERROR_OOM;
				}
				s->v[p-data] = '*';
				s->codename = s->v;
				s->codename_len = p-data;
				snapshot = *p == '=';
			} else
				/* last_dist not changed on purpose */
				continue;
		}
		/* found it to belong to distribution s */
		if (s->seen) {
			assert (last_dist != NULL);
			assert (!onlyone);
			continue;
		}
		if (s->seensnapshot && !snapshot) {
			s->seen = true;
			s->this.all += filesize;
			assert (last_dist != NULL);
			assert (!onlyone);
			continue;
		}
		/* distribution seen for this file the first time */
		if (last_dist != NULL) {
			if (onlyone) {
				last_dist->withsnapshots.onlyhere -= filesize;
				if (last_dist->seen)
					last_dist->this.onlyhere -= filesize;
				onlyone = false;
			}
			assert (filesize != 0);
		} else {
			/* and this is the first time
			 * we are interested in the file */
			filesize = files_getsize(key);
			assert (filesize != 0);
			if (onlyone)
				onlyall += filesize;
			all += filesize;
		}
		last_dist = s;
		if (snapshot) {
			s->seensnapshot = true;
		} else {
			s->seen = true;
			last_dist->this.all += filesize;
			if (onlyone)
				last_dist->this.onlyhere += filesize;
		}
		last_dist->withsnapshots.all += filesize;
		if (onlyone)
			last_dist->withsnapshots.onlyhere += filesize;
	}
	free(last_file);
	*all_p = all;
	*onlyall_p = onlyall;
	return RET_OK;
}

retvalue sizes_distributions(struct distribution *alldistributions, bool specific) {
	struct cursor *cursor;
	retvalue result, r;
	struct distribution_sizes *ds = NULL, **lds = &ds, *s;
	struct distribution *d;
	unsigned long long all = 0, onlyall = 0;

	for (d = alldistributions ; d != NULL ; d = d->next) {
		if (!d->selected)
			continue;
		s = zNEW(struct distribution_sizes);
		if (FAILEDTOALLOC(s)) {
			distribution_sizes_freelist(ds);
			return RET_ERROR_OOM;
		}
		s->codename = d->codename;
		s->codename_len = strlen(d->codename);
		*lds = s;
		lds = &s->next;
	}
	if (ds == NULL)
		return RET_NOTHING;
	r = table_newglobalcursor(rdb_references, true, &cursor);
	if (!RET_IS_OK(r)) {
		distribution_sizes_freelist(ds);
		return r;
	}
	result = count_sizes(cursor, specific, ds, &all, &onlyall);
	r = cursor_close(rdb_references, cursor);
	RET_ENDUPDATE(result, r);
	if (RET_IS_OK(result)) {
		printf("%-15s %13s %13s %13s %13s\n",
				"Codename", "Size", "Only", "Size(+s)",
				"Only(+s)");
		for (s = ds ; s != NULL ; s = s->next) {
			printf("%-15s %13llu %13llu %13llu %13llu\n",
					s->codename,
					s->this.all,
					s->this.onlyhere,
					s->withsnapshots.all,
					s->withsnapshots.onlyhere);
		}
		if (specific && ds->next != NULL)
			printf("%-15s %13s %13s %13llu %13llu\n",
					"<all selected> ",
					"", "",
					all, onlyall);
	}
	distribution_sizes_freelist(ds);
	return result;
}
