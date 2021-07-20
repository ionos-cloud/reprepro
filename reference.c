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

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "database_p.h"
#include "pool.h"
#include "reference.h"

retvalue references_isused( const char *what) {
	return table_gettemprecord(rdb_references, what, NULL, NULL);
}

retvalue references_check(const char *referee, const struct strlist *filekeys) {
	int i;
	retvalue result, r;

	result = RET_NOTHING;
	for (i = 0 ; i < filekeys->count ; i++) {
		r = table_checkrecord(rdb_references,
				filekeys->values[i], referee);
		if (r == RET_NOTHING) {
			fprintf(stderr, "Missing reference to '%s' by '%s'\n",
					filekeys->values[i], referee);
			r = RET_ERROR;
		}
		RET_UPDATE(result, r);
	}
	return result;
}

/* add an reference to a file for an identifier. multiple calls */
retvalue references_increment(const char *needed, const char *neededby) {
	retvalue r;

	if (verbose >= 15)
		fprintf(stderr, "trace: references_insert(needed=%s, neededby=%s) called.\n",
		        needed, neededby);

	r = table_addrecord(rdb_references, needed,
			neededby, strlen(neededby), false);
	if (RET_IS_OK(r) && verbose > 8)
		printf("Adding reference to '%s' by '%s'\n", needed, neededby);
	return r;
}

/* remove reference for a file from a given reference */
retvalue references_decrement(const char *needed, const char *neededby) {
	retvalue r;

	r = table_removerecord(rdb_references, needed, neededby);
	if (r == RET_NOTHING)
		return r;
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr,
"Error while trying to removing reference to '%s' by '%s'\n",
				needed, neededby);
		return r;
	}
	if (verbose > 8)
		fprintf(stderr, "Removed reference to '%s' by '%s'\n",
				needed, neededby);
	if (RET_IS_OK(r)) {
		retvalue r2;
		r2 = pool_dereferenced(needed);
		RET_UPDATE(r, r2);
	}
	return r;
}

/* Add an reference by <identifier> for the given <files>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_insert(const char *identifier,
		const struct strlist *files, const struct strlist *exclude) {
	retvalue result, r;
	int i;

	if (verbose >= 15) {
		fprintf(stderr, "trace: references_insert(identifier=%s, files=[", identifier);
		for (i = 0 ; i < files->count ; i++) {
			fprintf(stderr, "%s%s", i == 0 ? "" : ", ", files->values[i]);
		}
		fprintf(stderr, "], exclude=%s", exclude == NULL ? "NULL" : "[");
		if (exclude != NULL) {
			for (i = 0 ; i < exclude->count ; i++) {
				fprintf(stderr, "%s%s", i == 0 ? "" : ", ", exclude->values[i]);
			}
		}
		fprintf(stderr, "%s) called.\n", exclude == NULL ? "" : "]");
	}

	result = RET_NOTHING;

	for (i = 0 ; i < files->count ; i++) {
		const char *filename = files->values[i];

		if (exclude == NULL || !strlist_in(exclude, filename)) {
			r = references_increment(filename, identifier);
			RET_UPDATE(result, r);
		}
	}
	return result;
}

/* add possible already existing references */
retvalue references_add(const char *identifier, const struct strlist *files) {
	int i;
	retvalue r;

	for (i = 0 ; i < files->count ; i++) {
		const char *filekey = files->values[i];
		r = table_addrecord(rdb_references, filekey,
				identifier, strlen(identifier), true);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

/* Remove reference by <identifier> for the given <oldfiles>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_delete(const char *identifier, const struct strlist *files, const struct strlist *exclude) {
	retvalue result, r;
	int i;

	assert (files != NULL);

	result = RET_NOTHING;

	for (i = 0 ; i < files->count ; i++) {
		const char *filekey = files->values[i];

		if (exclude == NULL || !strlist_in(exclude, filekey)) {
			r = references_decrement(filekey, identifier);
			RET_UPDATE(result, r);
		}
	}
	return result;

}

/* remove all references from a given identifier */
retvalue references_remove(const char *neededby) {
	struct cursor *cursor;
	retvalue result, r;
	const char *found_to, *found_by;
	size_t datalen, l;

	r = table_newglobalcursor(rdb_references, true, &cursor);
	if (!RET_IS_OK(r))
		return r;

	l = strlen(neededby);

	result = RET_NOTHING;
	while (cursor_nexttempdata(rdb_references, cursor,
				&found_to, &found_by, &datalen)) {

		if (datalen >= l && strncmp(found_by, neededby, l) == 0 &&
		    (found_by[l] == '\0' || found_by[l] == ' ')) {
			if (verbose > 8)
				fprintf(stderr,
"Removing reference to '%s' by '%s'\n",
					found_to, neededby);
			r = cursor_delete(rdb_references, cursor,
					found_to, NULL);
			RET_UPDATE(result, r);
			if (RET_IS_OK(r)) {
				r = pool_dereferenced(found_to);
				RET_ENDUPDATE(result, r);
			}
		}
	}
	r = cursor_close(rdb_references, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

/* dump all references to stdout */
retvalue references_dump(void) {
	struct cursor *cursor;
	retvalue result, r;
	const char *found_to, *found_by;

	r = table_newglobalcursor(rdb_references, true, &cursor);
	if (!RET_IS_OK(r))
		return r;

	result = RET_OK;
	while (cursor_nexttempdata(rdb_references, cursor,
	                               &found_to, &found_by, NULL)) {
		if (fputs(found_by, stdout) == EOF ||
		    putchar(' ') == EOF ||
		    puts(found_to) == EOF) {
			result = RET_ERROR;
			break;
		}
		result = RET_OK;
		if (interrupted()) {
			result = RET_ERROR_INTERRUPTED;
			break;
		}
	}
	r = cursor_close(rdb_references, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}
