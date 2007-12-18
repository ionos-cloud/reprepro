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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <sys/types.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "error.h"
#include "checksums.h"
#include "names.h"
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

retvalue checksums_init(/*@out@*/struct checksums **checksums_p, /*@only@*/char *size, /*@only@*/char *md5) {
	char *md5sum = calc_concatmd5andsize(md5, size);
	char **md5sum_p = (char**)checksums_p;

	free(md5);free(size);
	if( md5sum == NULL ) {
		return RET_ERROR_OOM;
	}
	*md5sum_p = md5sum;
	return RET_OK;
}

struct checksums *checksums_dup(const struct checksums *checksums) {
	const char *md5sum = (char*)checksums;
	char *result = strdup(md5sum);

	return (struct checksums *)result;
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

retvalue checksums_getfilesize(const struct checksums *checksums, off_t *size_p) {
	const char *md5sum = (char*)checksums;
	const char *p = md5sum;
	off_t filesize;

	/* over md5 part to the length part */
	while( *p != ' ' && *p != '\0' )
		p++;
	if( *p != ' ' ) {
		fprintf(stderr, "Cannot extract filesize from '%s'\n", md5sum);
		return RET_ERROR;
	}
	while( *p == ' ' )
		p++;
	filesize = 0;
	while( *p <= '9' && *p >= '0' ) {
		filesize = filesize*10 + (*p-'0');
		p++;
	}
	if( *p != '\0' ) {
		fprintf(stderr, "Cannot extract filesize from '%s'\n", md5sum);
		return RET_ERROR;
	}
	*size_p = filesize;
	return RET_OK;
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

retvalue checksums_linkorcopyfile(const char *dest, const char *orig, struct checksums **checksum_p) {
	char **md5sum_p = (char**)checksum_p;

	return md5sum_place(orig, dest, md5sum_p);
}

retvalue checksums_read(const char *fullfilename, /*@out@*/struct checksums **checksum_p) {
	char **md5sum_p = (char**)checksum_p;

	return md5sum_read(fullfilename, md5sum_p);
}

bool checksums_check(const struct checksums *checksums, const struct checksums *realchecksums, bool *improves) {
	const char *md5indatabase = (void*)checksums;
	const char *md5offile = (void*)realchecksums;

	if( improves != NULL )
		*improves = false;
	return strcmp(md5indatabase, md5offile) == 0;
}

void checksums_printdifferences(FILE *f, const struct checksums *expected, const struct checksums *got) {
	const char *md5expected = (void*)expected;
	const char *md5got = (void*)got;

	fprintf(f, "expected: %s, got: %s\n", md5expected, md5got);
}

retvalue checksums_combine(struct checksums **checksums, const struct checksums *by) {
	assert( checksums != checksums && by != by);
	return RET_OK;
}

void checksumsarray_done(struct checksumsarray *array) {
	if( array->names.count > 0 ) {
		size_t i;
		for( i = 0 ; i < array->names.count ; i++ ) {
			checksums_free(array->checksums[i]);
		}
	} else
		assert( array->checksums == NULL );
	strlist_done(&array->names);
	free(array->checksums);
}

retvalue checksumsarray_parse(struct checksumsarray *out, const struct strlist *lines, const char *filenametoshow) {
	retvalue r;
	int i;
	struct checksumsarray a;

	/* +1 because the caller is likely include an additional file later */
	r = strlist_init_n(lines->count+1, &a.names);
	if( RET_WAS_ERROR(r) )
		return r;
	a.checksums = calloc(lines->count, sizeof(struct checksums *));
	if( lines->count > 0 && a.checksums == NULL ) {
		strlist_done(&a.names);
		return RET_ERROR_OOM;
	}
	for( i = 0 ; i < lines->count ; i++ ) {
		char *filename, *md5sum;

		r = calc_parsefileline(lines->values[i], &filename, &md5sum);
		if( RET_WAS_ERROR(r) ) {
			if( r != RET_ERROR_OOM )
				fprintf(stderr, "Error was parsing %s\n",
						filenametoshow);
			if( i == 0 ) {
				free(a.checksums);
				a.checksums = NULL;
			}
			checksumsarray_done(&a);
			return r;
		}
		r = checksums_set(&a.checksums[i], md5sum);
		if( RET_WAS_ERROR(r) ) {
			free(filename);
			if( i == 0 ) {
				free(a.checksums);
				a.checksums = NULL;
			}
			checksumsarray_done(&a);
			return r;
		}
		r = strlist_add(&a.names, filename);
		if( RET_WAS_ERROR(r) ) {
			checksums_free(a.checksums[i]);
			a.checksums[i] = NULL;
			if( i == 0 ) {
				free(a.checksums);
				a.checksums = NULL;
			}
			checksumsarray_done(&a);
			return r;
		}
	}
	checksumsarray_move(out, &a);
	return RET_OK;
}

void checksumsarray_move(/*@out@*/struct checksumsarray *destination, struct checksumsarray *origin) {
	strlist_move(&destination->names, &origin->names);
	destination->checksums = origin->checksums;
	origin->checksums = NULL;
}

retvalue checksumsarray_include(struct checksumsarray *a, /*@only@*/char *name, const struct checksums *checksums) {
	retvalue r;
	struct checksums **n;
	int count = a->names.count;

	n = malloc((count+1) * sizeof(struct checksums*));
	if( n == NULL ) {
		free(name);
		return RET_ERROR_OOM;
	}
	n[0] = checksums_dup(checksums);
	if( n[0] == NULL ) {
		free(name);
		free(n);
		return RET_ERROR_OOM;
	}
	r = strlist_include(&a->names, name);
	if( !RET_IS_OK(r) ) {
		free(n);
		return r;
	}
	assert( a->names.count == count + 1 );
	memcpy(&n[1], a->checksums, count*sizeof(struct checksums*));
	free(a->checksums);
	a->checksums = n;
	return RET_OK;
}

/* check if the file has the given md5sum (only cheap tests like size),
 * RET_NOTHING means file does not exist, RET_ERROR_WRONG_MD5 means wrong size */
retvalue checksums_cheaptest(const char *fullfilename, const struct checksums *checksums) {
	off_t expectedsize;
	retvalue r;
	int i;
	struct stat s;

	i = stat(fullfilename, &s);
	if( i < 0 ) {
		i = errno;
		if( i == EACCES || i == ENOENT )
			return RET_NOTHING;
		else {
			fprintf(stderr,"Error %d stating '%s': %s!\n",
					i, fullfilename, strerror(i));
			return RET_ERRNO(i);
		}
	}

	r = checksums_getfilesize(checksums, &expectedsize);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;

	if( s.st_size == expectedsize )
		return RET_OK;
	else
		return RET_ERROR_WRONG_MD5;
}
