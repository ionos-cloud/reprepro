/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007 Bernhard R. Link
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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "names.h"
#include "chunks.h"
#include "readtextfile.h"
#include "checksums.h"
#include "diffindex.h"


void diffindex_free(struct diffindex *diffindex) {
	int i;

	if (diffindex == NULL)
		return;
	checksums_free(diffindex->destination);
	for (i = 0 ; i < diffindex->patchcount ; i ++) {
		checksums_free(diffindex->patches[i].frompackages);
		free(diffindex->patches[i].name);
		checksums_free(diffindex->patches[i].checksums);
	}
	free(diffindex);
}

static void parse_sha1line(const char *p, /*@out@*/struct hashes *hashes, /*@out@*/const char **rest) {
	setzero(struct hashes, hashes);

	hashes->hashes[cs_sha1sum].start = p;
	while ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')
			|| (*p >= '0' && *p <= '9'))
		p++;
	hashes->hashes[cs_sha1sum].len = p - hashes->hashes[cs_sha1sum].start;
	while (*p == ' ' || *p == '\t')
		p++;
	hashes->hashes[cs_length].start = p;
	while (*p >= '0' && *p <= '9')
		p++;
	hashes->hashes[cs_length].len = p - hashes->hashes[cs_length].start;
	while (*p == ' ' || *p == '\t')
		p++;
	*rest = p;
}

static inline retvalue add_current(const char *diffindexfile, struct diffindex *n, const char *current) {
	struct hashes hashes;
	const char *p;
	retvalue r;

	parse_sha1line(current, &hashes, &p);
	if (hashes.hashes[cs_sha1sum].len == 0
			|| hashes.hashes[cs_length].len == 0
			|| *p != '\0') {
		r = RET_ERROR;
	} else
		r = checksums_initialize(&n->destination, hashes.hashes);
	ASSERT_NOT_NOTHING(r);
	if (RET_WAS_ERROR(r))
		fprintf(stderr, "Error parsing SHA1-Current in '%s'!\n",
				diffindexfile);
	return r;
}

static inline retvalue add_patches(const char *diffindexfile, struct diffindex *n, const struct strlist *patches) {
	int i;

	assert (patches->count == n->patchcount);

	for (i = 0 ; i < n->patchcount; i++) {
		struct hashes hashes;
		const char *patchname;
		retvalue r;

		parse_sha1line(patches->values[i], &hashes, &patchname);
		if (hashes.hashes[cs_sha1sum].len == 0
				|| hashes.hashes[cs_length].len == 0
				|| *patchname == '\0') {
			r = RET_ERROR;
		} else
			r = checksums_initialize(&n->patches[i].checksums,
					hashes.hashes);
		ASSERT_NOT_NOTHING(r);
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Error parsing SHA1-Patches line %d in '%s':!\n'%s'\n",
				i, diffindexfile, patches->values[i]);
			return r;
		}
		n->patches[i].name = strdup(patchname);
		if (FAILEDTOALLOC(n))
			return RET_ERROR_OOM;
	}
	return RET_OK;
}

static inline retvalue add_history(const char *diffindexfile, struct diffindex *n, const struct strlist *history) {
	int i, j;

	for (i = 0 ; i < history->count ; i++) {
		struct hashes hashes;
		const char *patchname;
		struct checksums *checksums;
		retvalue r;

		parse_sha1line(history->values[i], &hashes, &patchname);
		if (hashes.hashes[cs_sha1sum].len == 0
				|| hashes.hashes[cs_length].len == 0
				|| *patchname == '\0') {
			r = RET_ERROR;
		} else
			r = checksums_initialize(&checksums,
					hashes.hashes);
		ASSERT_NOT_NOTHING(r);
		if (RET_WAS_ERROR(r)) {
			fprintf(stderr,
"Error parsing SHA1-History line %d in '%s':!\n'%s'\n",
				i, diffindexfile, history->values[i]);
			return r;
		}
		j = 0;
		while (j < n->patchcount && strcmp(n->patches[j].name,
					patchname) != 0)
			j++;
		if (j >= n->patchcount) {
			fprintf(stderr,
"'%s' lists '%s' in history but not in patches!\n",
					diffindexfile, patchname);
			checksums_free(checksums);
			continue;
		}
		if (n->patches[j].frompackages != NULL) {
			fprintf(stderr,
"Warning: '%s' lists multiple histories for '%s'!\nOnly using last one!\n",
					diffindexfile, patchname);
			checksums_free(n->patches[j].frompackages);
		}
		n->patches[j].frompackages = checksums;
	}
	return RET_OK;
}

retvalue diffindex_read(const char *diffindexfile, struct diffindex **out_p) {
	retvalue r;
	char *chunk, *current;
	struct strlist history, patches;
	struct diffindex *n;

	r = readtextfile(diffindexfile, diffindexfile, &chunk, NULL);
	ASSERT_NOT_NOTHING(r);
	if (RET_WAS_ERROR(r))
		return r;
	r = chunk_getextralinelist(chunk, "SHA1-History", &history);
	if (r == RET_NOTHING) {
		fprintf(stderr, "'%s' misses SHA1-History field\n",
				diffindexfile);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		free(chunk);
		return r;
	}
	r = chunk_getextralinelist(chunk, "SHA1-Patches", &patches);
	if (r == RET_NOTHING) {
		fprintf(stderr, "'%s' misses SHA1-Patches field\n",
				diffindexfile);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		free(chunk);
		strlist_done(&history);
		return r;
	}
	r = chunk_getvalue(chunk, "SHA1-Current", &current);
	free(chunk);
	if (r == RET_NOTHING) {
		fprintf(stderr, "'%s' misses SHA1-Current field\n",
				diffindexfile);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		strlist_done(&history);
		strlist_done(&patches);
		return r;
	}
	n = calloc(1, sizeof(struct diffindex) +
			patches.count * sizeof(struct diffindex_patch));
	if (FAILEDTOALLOC(n)) {
		strlist_done(&history);
		strlist_done(&patches);
		free(current);
		return r;
	}
	n->patchcount = patches.count;
	r = add_current(diffindexfile, n, current);
	if (RET_IS_OK(r))
		r = add_patches(diffindexfile, n, &patches);
	if (RET_IS_OK(r))
		r = add_history(diffindexfile, n, &history);
	ASSERT_NOT_NOTHING(r);
	strlist_done(&history);
	strlist_done(&patches);
	free(current);
	if (RET_IS_OK(r))
		*out_p = n;
	else
		diffindex_free(n);
	return r;
}
