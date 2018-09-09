/*  This file is part of "reprepro"
 *  Copyright (C) 2012 Bernhard R. Link
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
#include <string.h>
#include <stdlib.h>
#include "error.h"
#include "ignore.h"
#include "strlist.h"
#include "chunks.h"
#include "files.h"
#include "debfile.h"
#include "binaries.h"
#include "descriptions.h"
#include "md5.h"

/* get the description from a .(u)deb file */
static retvalue description_from_package(const char *control, char **description_p) {
	struct strlist filekeys;
	char *filename;
	char *deb_control;
	retvalue r;

	r = binaries_getfilekeys(control, &filekeys);
	if (r == RET_NOTHING)
		r = RET_ERROR;
	if (RET_WAS_ERROR(r))
		return r;
	if (filekeys.count != 1) {
		fprintf(stderr, "Strange number of files for binary package: %d\n",
				filekeys.count);
		strlist_done(&filekeys);
		return RET_ERROR;
	}
	filename = files_calcfullfilename(filekeys.values[0]);
	strlist_done(&filekeys);
	if (FAILEDTOALLOC(filename))
		return RET_ERROR_OOM;
	if (verbose > 7) {
		fprintf(stderr, "Reading '%s' to extract description...\n",
				filename);
	}
	r = extractcontrol(&deb_control, filename);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		free(filename);
		return r;
	}
	r = chunk_getwholedata(deb_control, "Description", description_p);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Not found any Description within file '%s'!\n",
				filename);
	}
	free(filename);
	free(deb_control);
	return r;
}

static const char tab[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

/* This only matches the official one if the description is well-formed enough.
 * If it has less or more leading spaces or anything else our reading has stripped,
 * it will not match.... */
static void description_genmd5(const char *description, /*@out@*/ char *d, size_t len) {
	struct MD5Context context;
	unsigned char md5buffer[MD5_DIGEST_SIZE];
	int i;

	assert (len == 2*MD5_DIGEST_SIZE + 1);
	MD5Init(&context);
	MD5Update(&context, (const unsigned char*)description, strlen(description));
	MD5Update(&context, (const unsigned char*)"\n", 1);
	MD5Final(md5buffer, &context);
	for (i=0 ; i < MD5_DIGEST_SIZE ; i++) {
		*(d++) = tab[md5buffer[i] >> 4];
		*(d++) = tab[md5buffer[i] & 0xF];
	}
	*d = '\0';
}

/* Currently only normalizing towards a full Description is supported,
 * the cached description is not yet used, long descriptions are not stored elsewhere
 * and thus also no reference counting is done. */

retvalue description_addpackage(struct target *target, const char *package, const char *control, char **control_p) {
	char *description, *description_md5, *deb_description, *newcontrol;
	struct fieldtoadd *todo;
	size_t dlen;
	retvalue r;

	/* source packages have no descriptions */
	if (target->packagetype == pt_dsc)
		return RET_NOTHING;

	r = chunk_getwholedata(control, "Description", &description);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Strange control data for '%s': no Description at all\n",
				package);
		return RET_NOTHING;
	}
	if (strchr(description, '\n') != NULL) {
		/* there already is a long description, nothing to do */
		free(description);
		return RET_NOTHING;
	}
	dlen = strlen(description);

	r = chunk_getwholedata(control, "Description-md5", &description_md5);
	if (RET_WAS_ERROR(r)) {
		free(description);
		return r;
	}
	if (r == RET_NOTHING) {
		/* only short description and no -md5?
		 * unusual but can happen, especially with .udeb */
		free(description);
		return RET_NOTHING;
	}
	r = description_from_package(control, &deb_description);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		fprintf(stderr, "Cannot retrieve long description for package '%s' out of package's files!\n",
				package);
		free(description);
		free(description_md5);
		/* not finding the .deb file is not fatal */
		return RET_NOTHING;
	}
	/* check if the existing short description matches the found one */
	if (strncmp(description, deb_description, dlen) != 0) {
		fprintf(stderr,
"Short Description of package '%s' does not match\n"
"the start of the long descriptiongfound in the .deb\n",
				package);
		//if (!force) {
			free(description);
			free(description_md5);
			free(deb_description);
			/* not fatal, only not processed */
			return RET_NOTHING;
		//}
	}
	if (strlen(deb_description) == dlen) {
		/* nothing new, only a short description in the .deb, too: */
		free(description);
		free(description_md5);
		free(deb_description);
		return RET_NOTHING;
	}
	free(description);
	/* check if Description-md5 matches */
	if (description_md5 != NULL) {
		char found[2 * MD5_DIGEST_SIZE + 1];

		description_genmd5(deb_description, found, sizeof(found));
		if (strcmp(found, description_md5) != 0) {
			fprintf(stderr,
"Description-md5 of package '%s' does not match\n"
"the md5 of the description found in the .deb\n"
"('%s' != '%s')!\n",
				package, description_md5, found);
			//if (!force) {
				free(description_md5);
				/* not fatal, only not processed */
				free(deb_description);
				return RET_NOTHING;
			//}
		}
		free(description_md5);
	}

	todo = deletefield_new("Description-md5", NULL);
	if (!FAILEDTOALLOC(todo))
		todo = addfield_new("Description", deb_description, todo);
	newcontrol = chunk_replacefields(control, todo, "Description", false);
	addfield_free(todo);
	free(deb_description);
	if (FAILEDTOALLOC(newcontrol))
		return RET_ERROR_OOM;
	*control_p = newcontrol;
	return RET_OK;
}
