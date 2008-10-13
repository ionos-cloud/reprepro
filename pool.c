/*  This file is part of "reprepro"
 *  Copyright (C) 2008 Bernhard R. Link
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
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "atoms.h"
#include "pool.h"
#include "reference.h"
#include "files.h"

/* for now save them only in memory. In later times some way to store
 * them on disk would be nice */

static void **file_changes_per_component = NULL;
static void *legacy_file_changes = NULL;
bool pool_havedereferenced = false;

#define pl_ADDED 1
#define pl_UNREFERENCED 2

static int legacy_compare(const void *a, const void *b) {
	const char *v1 = a, *v2 = b;
	v1++;
	v2++;
	return strcmp(v1,v2);
}

static retvalue pool_addfile(const char *filename) {
	char **p; size_t l;

	p = tsearch(filename - 1, &legacy_file_changes, legacy_compare);
	if( p == NULL )
		return RET_ERROR_OOM;
	if( *p == filename - 1 ) {
		l = strlen(filename);
		*p = malloc(l + 2);
		if (*p == NULL)
			return RET_ERROR_OOM;
		**p = pl_ADDED;
		memcpy((*p) + 1, filename, l + 1);
	} else {
		**p |= pl_ADDED;
	}
	return RET_OK;
};

retvalue pool_dereferenced(const char *filename) {
	char **p; size_t l;

	p = tsearch(filename - 1, &legacy_file_changes, legacy_compare);
	if( p == NULL )
		return RET_ERROR_OOM;
	if( *p == filename - 1 ) {
		l = strlen(filename);
		*p = malloc(l + 2);
		if (*p == NULL)
			return RET_ERROR_OOM;
		**p = pl_UNREFERENCED;
		memcpy((*p) + 1, filename, l + 1);
		pool_havedereferenced = true;
	} else {
		**p |= pl_UNREFERENCED;
	}
	return RET_OK;
};

static struct database *d;
static retvalue result;
static bool first;

static void removeifunreferenced(const void *nodep, const VISIT which, const int depth) {
	const char *node, *filekey;
	retvalue r;

	if( which != leaf && which != postorder)
		return;

	if( interrupted() )
		return;

	node = *(const char **)nodep;
	filekey = node + 1;
	if( (*node & pl_UNREFERENCED) == 0 )
		return;
	r = references_isused(d, filekey);
	if( r != RET_NOTHING )
		return;

	if( verbose >= 0 && first ) {
		printf("Deleting files no longer referenced...\n");
		first = false;
	}
	r = files_deleteandremove(d, filekey, true);
	if( r == RET_NOTHING ) {
		fprintf(stderr, "To be forgotten filekey '%s' was not known.\n",
				filekey);
		r = RET_ERROR_MISSING;
	}
	RET_UPDATE(result, r);
}

retvalue pool_removeunreferenced(struct database *database) {
	d = database;
	result = RET_NOTHING;
	first = true;
	twalk(legacy_file_changes, removeifunreferenced);
	d = NULL;
	if( interrupted() )
		result = RET_ERROR_INTERRUPTED;
	return result;
}
