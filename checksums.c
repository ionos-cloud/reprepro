/*  This file is part of "reprepro"
 *  Copyright (C) 2007 Bernhard R. Link
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

#include <stdint.h>
#include <sys/types.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "error.h"
#include "checksums.h"
#include "md5sum.h"

/* yet only an evil stub, putting the old md5sums in faked struct pointers
 * so the rest of the code can adapt... */

void checksums_free(struct checksums *checksums) {
	free(checksums);
}

/* create a checksum record from an md5sum: */
retvalue checksums_set(struct checksums **result, char *md5sum) {
	*result = (void *)md5sum;
	return RET_OK;
}

retvalue checksums_get(const struct checksums *checksums, enum checksumtype type, char **result) {
	char *md5sum;

	if( type != cs_md5sum )
		return RET_NOTHING;

	md5sum = strdup((char *)checksums);
	if( md5sum == NULL )
		return RET_ERROR_OOM;
	*result = md5sum;
	return RET_OK;
}

retvalue checksums_getpart(const struct checksums *checksums, enum checksumtype type, const char **sum_p, size_t *size_p) {
	const char *p;

	if( type == cs_count ) {
		p = (char *)checksums;
		while( *p != '\0' && *p != ' ' )
			p++;
		if( *p != ' ' )
			return RET_ERROR;
		p++;
		*size_p = strlen(p);
		*sum_p = p;
		return RET_OK;
	} else if( type == cs_md5sum ) {
		p = (char *)checksums;
		while( *p != '\0' && *p != ' ' )
			p++;
		if( *p != ' ' )
			return RET_ERROR;
		*size_p = p-(char*)checksums;
		*sum_p = (char*)checksums;
		return RET_OK;
	} else
		return RET_NOTHING;
}

bool checksums_matches(const struct checksums *checksums,enum checksumtype type, const char *sum) {
	if( type == cs_md5sum )
		return strcmp((char*)checksums,sum) == 0;
	else
		return true;
}

retvalue checksums_copyfile(const char *dest, const char *orig, struct checksums **checksum_p) {
	char **md5sum_p = (char**)checksum_p;

	return md5sum_copy(orig, dest, md5sum_p);
}

retvalue checksums_read(const char *fullfilename, /*@out@*/struct checksums **checksum_p) {
	char **md5sum_p = (char**)checksum_p;

	return md5sum_read(fullfilename, md5sum_p);
}

bool checksums_check(const struct checksums *checksums, const struct checksums *realchecksums, bool *improves) {
	const char *md5indatabase = (void*)checksums;
	const char *md5offile = (void*)realchecksums;

	*improves = false;
	return strcmp(md5indatabase, md5offile) == 0;
}

void checksums_printdifferences(FILE *f, const struct checksums *expected, const struct checksums *got) {
	const char *md5expected = (void*)expected;
	const char *md5got = (void*)got;

	fprintf(f, "expected: %s, got: %s\n", md5expected, md5got);
}

retvalue checksums_combine(struct checksums **checksums, struct checksums *by) {
	free(by);
	assert( checksums != checksums );
}
