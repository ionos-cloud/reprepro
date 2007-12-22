/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007 Bernhard R. Link
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
#include <fcntl.h>

#define CHECKSUMS_CONTEXT visible
#include "error.h"
#include "mprintf.h"
#include "checksums.h"
#include "filecntl.h"
#include "names.h"
#include "dirs.h"
#include "md5sum.h"

/* The internal representation of a checksum, as written to the databases,
 * is \(:[1-9a-z]:[^ ]\+ \)*[0-9a-fA-F]\+ [0-9]\+
 * first some hashes, whose type is determined by a single character
 * (also yet unknown hashes are supported and should be preserved, but are
 *  not generated)
 * after that the md5sum and finaly the size in dezimal representation.
 *
 * Checksums are parsed and stored in a structure for fast access of their
 * known parts:
 */

struct checksums {
	struct { unsigned short ofs;
		unsigned short len;
	} parts[cs_count+1];
	char representation[];
};

static const char * const hash_name[cs_count+1] =
	{ "md5", "sha1", "size" };

void checksums_free(struct checksums *checksums) {
	free(checksums);
}

/* create a checksum record from an md5sum: */
retvalue checksums_set(struct checksums **result, char *md5sum) {
	retvalue r;

	// TODO: depreceate this function?
	// or reimplement it? otherwise the error messaged might be even
	// wronger. (and checking things from the database could be done
	// faster than stuff from outside)
	r = checksums_parse(result, md5sum);
	free(md5sum);
	return r;
}

retvalue checksums_init(/*@out@*/struct checksums **checksums_p, /*@only@*/char *size, /*@only@*/char *md5) {
	size_t md5len;
	size_t sizelen;
	const char *p;
	struct checksums *n;

	while( *size == '0' && size[1] >= '0' && size[1] <= '9' )
		size++;

	md5len = strlen(md5);
	sizelen = strlen(size);

	p = md5;
	while( (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')
			                || (*p >= 'A' && *p <= 'F') )
		p++;
	if( *p != '\0' ) {
		// TODO: find way to give more meaningfull error message
		fprintf(stderr, "Invalid md5 hash: '%s'\n", md5);
		free(md5); free(size);
		return RET_ERROR;
	}
	p = size;
	while( (*p >= '0' && *p <= '9') )
		p++;
	if( *p != '\0' ) {
		// TODO: find way to give more meaningfull error message
		fprintf(stderr, "Invalid size: '%s'\n", size);
		free(md5); free(size);
		return RET_ERROR;
	}

	n = malloc(sizeof(struct checksums) + md5len + sizelen + 2);
	if( n == NULL ) {
		free(md5); free(size);
		return RET_ERROR_OOM;
	}

	memset(n, 0, sizeof(struct checksums));
	n->parts[cs_md5sum].ofs = 0;
	n->parts[cs_md5sum].len = md5len;
	memcpy(n->representation, md5, md5len);
	n->representation[md5len] = ' ';
	n->parts[cs_count].ofs = md5len+1;
	n->parts[cs_count].len = sizelen;
	memcpy(n->representation + md5len + 1, size, sizelen + 1);
	// n->representation[md5len + 1 + sizelen] = '\0';

	free(md5);free(size);
	*checksums_p = n;
	return RET_OK;
}

retvalue checksums_parse(struct checksums **checksums_p, const char *combinedchecksum) {
	struct checksums *n;
	size_t len = strlen(combinedchecksum);
	const char *p = combinedchecksum;
	char *d;
	char type;
	const char *start;

	n = malloc(sizeof(struct checksums) + len + 1);
	if( n == NULL )
		return RET_ERROR_OOM;
	memset(n, 0, sizeof(struct checksums));
	d = n->representation;
	while( *p == ':' ) {

		p++;
		if( p[0] == '\0' || p[1] != ':' ) {
			// TODO: how to get some context in this?
			fprintf(stderr,
"Malformed checksums representation: '%s'!\n",
				combinedchecksum);
			free(n);
			return RET_ERROR;
		}
		type = p[0];
		p += 2;
		*(d++) = ':';
		*(d++) = type;
		*(d++) = ':';
		if( type == '1' ) {
			start = d;
			n->parts[cs_sha1sum].ofs = d - n->representation;
			while( *p != ' ' && *p != '\0' )
				*(d++) = *(p++);
			n->parts[cs_sha1sum].len = d - start;
		} else {
			while( *p != ' ' && *p != '\0' )
				*(d++) = *(p++);
		}

		*(d++) = ' ';
		while( *p == ' ' )
			p++;
	}
	n->parts[cs_md5sum].ofs = d - n->representation;
	start = d;
	while( *p != ' ' && *p != '\0' ) {
		if( (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ) {
			*(d++) = *(p++);
		} else if( *p >= 'A' && *p <= 'F' ) {
			*(d++) = *(p++) + ('a' - 'A');
		} else {
			// TODO: how to get some context in this?
			fprintf(stderr,
"Malformed checksums representation (invalid md5sum): '%s'!\n",
				combinedchecksum);
			free(n);
			return RET_ERROR;
		}
	}
	n->parts[cs_md5sum].len = d - start;
	*(d++) = ' ';
	while( *p == ' ' )
		p++;
	n->parts[cs_count].ofs = d - n->representation;
	while( *p == '0' && ( p[1] >= '0' && p[1] <= '9' ) )
		p++;
	start = d;
	while( *p != '\0' ) {
		if( *p >= '0' && *p <= '9' ) {
			*(d++) = *(p++);
		} else {
			// TODO: how to get some context in this?
			fprintf(stderr,
"Malformed checksums representation (invalid size): '%s'!\n",
				combinedchecksum);
			free(n);
			return RET_ERROR;
		}
	}
	n->parts[cs_count].len = d - start;
	if( d == start ) {
		// TODO: how to get some context in this?
		fprintf(stderr,
"Malformed checksums representation (no size): '%s'!\n",
				combinedchecksum);
		free(n);
		return RET_ERROR;
	}
	*d = '\0';
	assert( d - n->representation <= len );
	*checksums_p = n;
	return RET_OK;
}

struct checksums *checksums_dup(const struct checksums *checksums) {
	struct checksums *n;
	size_t len;

	assert( checksums != NULL );
	len = checksums->parts[cs_count].ofs + checksums->parts[cs_count].len;
	assert( checksums->representation[len] == '\0' );

	n = malloc(sizeof(struct checksums) + len + 1);
	if( n == NULL )
		return n;
	memcpy(n, checksums, sizeof(struct checksums) + len + 1);
	assert( n->representation[len] == '\0' );
	return n;
}

bool checksums_getpart(const struct checksums *checksums, enum checksumtype type, const char **sum_p, size_t *size_p) {

	assert( type >= cs_md5sum && type <= cs_count );

	if( checksums->parts[type].len == 0 )
		return false;
	*size_p = checksums->parts[type].len;
	*sum_p = checksums->representation + checksums->parts[type].ofs;
	return true;
}

bool checksums_gethashpart(const struct checksums *checksums, enum checksumtype type, const char **hash_p, size_t *hashlen_p, const char **size_p, size_t *sizelen_p) {
	assert( type >= 0 );
	assert( type < cs_count );
	if( checksums->parts[type].len == 0 )
		return false;
	*hashlen_p = checksums->parts[type].len;
	*hash_p = checksums->representation + checksums->parts[type].ofs;
	*sizelen_p = checksums->parts[cs_count].len;
	*size_p = checksums->representation + checksums->parts[cs_count].ofs;
	return true;
}

retvalue checksums_get(const struct checksums *checksums, enum checksumtype type, char **result) {
	const char *hash, *size;
	size_t hashlen, sizelen;

	if( checksums_gethashpart(checksums, type,
				&hash, &hashlen, &size, &sizelen) ) {
		char *checksum;

		checksum = malloc(hashlen + sizelen + 2);
		if( checksum == NULL )
			return RET_ERROR_OOM;
		memcpy(checksum, hash, hashlen);
		checksum[hashlen] = ' ';
		memcpy(checksum+hashlen+1, size, sizelen);
		checksum[hashlen+1+sizelen] = '\0';
		*result = checksum;
		return RET_OK;

	} else
		return RET_NOTHING;
}


retvalue checksums_getcombined(const struct checksums *checksums, /*@out@*/const char **data_p, /*@out@*/size_t *size_p) {
	size_t len;

	assert( checksums != NULL );
	len = checksums->parts[cs_count].ofs + checksums->parts[cs_count].len;
	assert( checksums->representation[len] == '\0' );

	*data_p = checksums->representation;
	*size_p = len + 1;
	return RET_OK;
}

retvalue checksums_getfilesize(const struct checksums *checksums, off_t *size_p) {
	const char *p = checksums->representation + checksums->parts[cs_count].ofs;
	off_t filesize;

	filesize = 0;
	while( *p <= '9' && *p >= '0' ) {
		filesize = filesize*10 + (*p-'0');
		p++;
	}
	assert( *p == '\0' );
	*size_p = filesize;
	return RET_OK;
}

bool checksums_matches(const struct checksums *checksums,enum checksumtype type, const char *sum) {
	size_t len = checksums->parts[type].len;

	assert( type >= 0 && type < cs_count );

	if( len == 0 )
		return true;

	if( strncmp(sum, checksums->representation + checksums->parts[type].ofs,
				len) != 0 )
		return false;
	if( sum[len] != ' ' )
		return false;
	/* assuming count is the last part: */
	if( strncmp(sum + len + 1,
	           checksums->representation + checksums->parts[cs_count].ofs,
		   checksums->parts[cs_count].len + 1) != 0 )
		return false;
	return true;
}

retvalue checksums_copyfile(const char *dest, const char *orig, struct checksums **checksum_p) {
	char *md5sum;
	retvalue r;

	r = md5sum_copy(orig, dest, &md5sum);
	if( RET_IS_OK(r) )
		r = checksums_set(checksum_p, md5sum);
	return r;
}

retvalue checksums_linkorcopyfile(const char *dest, const char *orig, struct checksums **checksum_p) {
	char *md5sum;
	retvalue r;

	r = md5sum_place(orig, dest, &md5sum);
	if( RET_IS_OK(r) )
		r = checksums_set(checksum_p, md5sum);
	return r;
}

static inline bool differ(const struct checksums *a, const struct checksums *b, enum checksumtype type) {
	if( a->parts[type].len == 0 || b->parts[type].len == 0 )
		return false;
	if( a->parts[type].len != b->parts[type].len )
		return true;
	return memcmp(a->representation + a->parts[type].ofs,
			b->representation + b->parts[type].ofs,
			a->parts[type].len) != 0;
}

bool checksums_check(const struct checksums *checksums, const struct checksums *realchecksums, bool *improves) {
	enum checksumtype type;
	bool additional = false;

	for( type = cs_md5sum ; type <= cs_count ; type++ ) {
		if( differ(checksums, realchecksums, type) )
			return false;
		if( checksums->parts[type].len == 0 || 
		    realchecksums->parts[type].len != 0 )
			additional = true;
	}
	if( improves != NULL )
		*improves = additional;
	return true;
}

void checksums_printdifferences(FILE *f, const struct checksums *expected, const struct checksums *got) {
	enum checksumtype type;

	for( type = cs_md5sum ; type <= cs_count ; type++ ) {
		if( differ(expected, got, type) ) {
			fprintf(f, "%s expected: %.*s, got: %.*s\n",
					hash_name[type],
					(int)expected->parts[type].len,
					expected->representation +
					 expected->parts[type].ofs,
					(int)got->parts[type].len,
					got->representation +
					 got->parts[type].ofs);
		}
	}
}

retvalue checksums_combine(struct checksums **checksums_p, const struct checksums *by) {
	struct checksums *old = *checksums_p, *n;
	size_t len = old->parts[cs_count].ofs + old->parts[cs_count].len
		    + by->parts[cs_count].ofs + by->parts[cs_count].len;
	const char *o, *b, *start;
	char *d, typeid;

	n = malloc(sizeof(struct checksums)+ len + 1);
	if( n == NULL )
		return RET_ERROR_OOM;
	memset(n, 0, sizeof(struct checksums));
	o = old->representation;
	b = by->representation;
	d = n->representation;
	while( *o == ':' || *b == ':' ) {
		if( b[0] != ':' || (o[0] == ':' && o[1] <= b[1]) ) {
			*(d++) = *(o++);
			typeid = *o;
			*(d++) = *(o++);
			*(d++) = *(o++);
			if( typeid == '1' ) {
				start = d;
				n->parts[cs_sha1sum].ofs = d - n->representation;
				while( *o != ' ' && *o != '\0' )
					*(d++) = *(o++);
				n->parts[cs_sha1sum].len = d - start;
			} else
				while( *o != ' ' && *o != '\0' )
					*(d++) = *(o++);
			assert( *o == ' ' );
			if( *o == ' ' )
				*(d++) = *(o++);

			if( b[0] == ':' && typeid == b[1] ) {
				while( *b != ' ' && *b != '\0' )
					b++;
				assert( *b == ' ' );
				if( *b == ' ' )
					b++;
			}
		} else {
			*(d++) = *(b++);
			typeid = *b;
			*(d++) = *(b++);
			*(d++) = *(b++);
			if( typeid == '1' ) {
				start = d;
				n->parts[cs_sha1sum].ofs = d - n->representation;
				while( *b != ' ' && *b != '\0' )
					*(d++) = *(b++);
				n->parts[cs_sha1sum].len = d - start;
			} else
				while( *b != ' ' && *b != '\0' )
					*(d++) = *(b++);
			assert( *b == ' ' );
			if( *b == ' ' )
				*(d++) = *(b++);
		}
	}
	/* now take md5sum from original code */
	n->parts[cs_md5sum].ofs = d - n->representation;
	start = d;
	while( *o != ' ' && *o != '\0' )
		*(d++) = *(o++);
	n->parts[cs_md5sum].len = d - start;
	assert( *o == ' ' );
	if( *o == ' ' )
		*(d++) = *(o++);
	/* and now the size */
	n->parts[cs_count].ofs = d - n->representation;
	start = d;
	while( *o != '\0' )
		*(d++) = *(o++);
	n->parts[cs_count].len = d - start;
	assert( d - n->representation <= len );
	*(d++) = '\0';
	*checksums_p = realloc(n, sizeof(struct checksums)
	                          + (d-n->representation));
	if( *checksums_p == NULL )
		*checksums_p = n;
	checksums_free(old);
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

/* copy, only checking file size, perhaps add some paranoia checks later */
static retvalue copy(const char *destination, const char *source, const struct checksums *checksums) {
	off_t filesize = 0;
	static const int bufsize = 16384;
	char *buffer = malloc(bufsize);
	ssize_t sizeread, towrite, written;
	const char *start;
	int e, i;
	int infd, outfd;

	if( buffer == NULL ) {
		return RET_ERROR_OOM;
	}

	infd = open(source, O_RDONLY);
	if( infd < 0 ) {
		e = errno;
		fprintf(stderr,"Error %d opening '%s': %s\n",
				e, source, strerror(e));
		free(buffer);
		return RET_ERRNO(e);
	}
	outfd = open(destination, O_NOCTTY|O_WRONLY|O_CREAT|O_EXCL, 0666);
	if( outfd < 0 ) {
		e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, destination, strerror(e));
		close(infd);
		free(buffer);
		return RET_ERRNO(e);
	}
	filesize = 0;
	do {
		sizeread = read(infd, buffer, bufsize);
		if( sizeread < 0 ) {
			e = errno;
			fprintf(stderr,"Error %d while reading %s: %s\n",
					e, source, strerror(e));
			free(buffer);
			close(infd); close(outfd);
			deletefile(destination);
			return RET_ERRNO(e);;
		}
		filesize += sizeread;
		towrite = sizeread;
		start = buffer;
		while( towrite > 0 ) {
			written = write(outfd, start, towrite);
			if( written < 0 ) {
				e = errno;
				fprintf(stderr,"Error %d while writing to %s: %s\n",
						e, destination, strerror(e));
				free(buffer);
				close(infd); close(outfd);
				deletefile(destination);
				return RET_ERRNO(e);;
			}
			towrite -= written;
			start += written;
		}
	} while( sizeread > 0 );
	free(buffer);
	i = close(infd);
	if( i != 0 ) {
		e = errno;
		fprintf(stderr,"Error %d reading %s: %s\n",
				e, source, strerror(e));
		close(outfd);
		deletefile(destination);
		return RET_ERRNO(e);;
	}
	i = close(outfd);
	if( i != 0 ) {
		e = errno;
		fprintf(stderr,"Error %d writing to %s: %s\n",
				e, destination, strerror(e));
		deletefile(destination);
		return RET_ERRNO(e);;
	}
	// TODO: check filesize to match checksums
	return RET_OK;
}

retvalue checksums_hardlink(const char *directory, const char *filekey, const char *sourcefilename, const struct checksums *checksums) {
	retvalue r;
	int i,e;
	char *fullfilename = calc_fullfilename(directory, filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;

	i = link(sourcefilename, fullfilename);
	e = errno;
	if( i != 0 && e == EEXIST )  {
		unlink(fullfilename);
		errno = 0;
		i = link(sourcefilename, fullfilename);
		e = errno;
	}
	if( i != 0 && ( e == EACCES || e == ENOENT || e == ENOTDIR ) )  {
		errno = 0;
		(void)dirs_make_parent(fullfilename);
		i = link(sourcefilename, fullfilename);
		e = errno;
	}
	if( i != 0 ) {
		if( e == EXDEV || e == EPERM || e == EMLINK ) {
			r = copy(fullfilename, sourcefilename, checksums);
			if( RET_WAS_ERROR(r) ) {
				free(fullfilename);
				return r;
			}
		} else {
			fprintf(stderr,
"Error %d creating hardlink of '%s' as '%s': %s\n",
				e, sourcefilename, fullfilename, strerror(e));
			free(fullfilename);
			return RET_ERRNO(e);
		}
	}
	free(fullfilename);
	return RET_OK;
}

void checksumscontext_init(struct checksumscontext *context) {
	MD5Init(&context->md5);
	SHA1Init(&context->sha1);
}

void checksumscontext_update(struct checksumscontext *context, const unsigned char *data, size_t len) {
	MD5Update(&context->md5, data, len);
	SHA1Update(&context->sha1, data, len);
}

static const char tab[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

retvalue checksums_from_context(struct checksums **out, struct checksumscontext *context) {
#define MD5_DIGEST_SIZE 16
	unsigned char md5buffer[MD5_DIGEST_SIZE], sha1buffer[SHA1_DIGEST_SIZE];
	char *d;
	unsigned int i;
	struct checksums *n;

	n = malloc(sizeof(struct checksums) + 2*MD5_DIGEST_SIZE
			+ 2*SHA1_DIGEST_SIZE + 26);
	if( n == NULL )
		return RET_ERROR_OOM;
	memset(n, 0, sizeof(struct checksums));
	d = n->representation;
	*(d++) = ':';
	*(d++) = '1';
	*(d++) = ':';
	n->parts[cs_sha1sum].ofs = 3;
	n->parts[cs_sha1sum].len = 2*SHA1_DIGEST_SIZE;
	SHA1Final(&context->sha1, sha1buffer);
	for( i = 0 ; i < SHA1_DIGEST_SIZE ; i++ ) {
		*(d++) = tab[sha1buffer[i] >> 4];
		*(d++) = tab[sha1buffer[i] & 0xF];
	}
	*(d++) = ' ';

	n->parts[cs_md5sum].ofs = 2*SHA1_DIGEST_SIZE+4;
	assert( d - n->representation == n->parts[cs_md5sum].ofs);
	n->parts[cs_md5sum].len = 2*MD5_DIGEST_SIZE;
	MD5Final(md5buffer, &context->md5);
	for( i=0 ; i < MD5_DIGEST_SIZE ; i++ ) {
		*(d++) = tab[md5buffer[i] >> 4];
		*(d++) = tab[md5buffer[i] & 0xF];
	}
	*(d++) = ' ';
	n->parts[cs_count].ofs = 2*MD5_DIGEST_SIZE + 2*SHA1_DIGEST_SIZE + 5;
	assert( d - n->representation == n->parts[cs_count].ofs);
	n->parts[cs_count].len = snprintf(d,
			2*MD5_DIGEST_SIZE + 2*SHA1_DIGEST_SIZE + 26
			- (d - n->representation), "%lld",
			(long long)context->sha1.count);
	assert( strlen(d) == n->parts[cs_count].len );
	*out = n;
	return RET_OK;
}

/* Collect missing checksums.
 * if the file is not there, return RET_NOTHING.
 * return RET_ERROR_WRONG_MD5 if already existing do not match */
retvalue checksums_complete(struct checksums **checksums_p, const char *directory, const char *filename) {
	retvalue r;
	char *fullfilename;
	struct checksums *realchecksums;
	bool improves;

	if( (*checksums_p)->parts[cs_md5sum].len != 0 &&
	    (*checksums_p)->parts[cs_sha1sum].len != 0 )
		return RET_OK;

	fullfilename = calc_dirconcat(directory, filename);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	r = checksums_cheaptest(fullfilename, realchecksums);
	if( !RET_IS_OK(r) ) {
		free(fullfilename);
		return r;
	}
	r = checksums_read(fullfilename, &realchecksums);
	free(fullfilename);
	if( !RET_IS_OK(r) )
		return r;
	if( checksums_check(*checksums_p, realchecksums, &improves) ) {
		assert(improves);

		r = checksums_combine(checksums_p, realchecksums);
		checksums_free(realchecksums);
		return r;
	} else {
		checksums_free(realchecksums);
		return RET_ERROR_WRONG_MD5;
	}
}

retvalue checksums_read(const char *fullfilename, /*@out@*/struct checksums **checksums_p) {
	struct checksumscontext context;
	static const int bufsize = 16384;
	unsigned char *buffer = malloc(bufsize);
	ssize_t sizeread;
	int e, i;
	int infd;

	if( buffer == NULL ) {
		return RET_ERROR_OOM;
	}

	checksumscontext_init(&context);

	infd = open(fullfilename, O_RDONLY);
	if( infd < 0 ) {
		e = errno;
		if( (e == EACCES || e == ENOENT) &&
				!isregularfile(fullfilename) )
			return RET_NOTHING;
		fprintf(stderr,"Error %d opening '%s': %s\n",
				e, fullfilename, strerror(e));
		free(buffer);
		return RET_ERRNO(e);
	}
	do {
		sizeread = read(infd, buffer, bufsize);
		if( sizeread < 0 ) {
			e = errno;
			fprintf(stderr,"Error %d while reading %s: %s\n",
					e, fullfilename, strerror(e));
			free(buffer);
			close(infd);
			return RET_ERRNO(e);;
		}
		checksumscontext_update(&context, buffer, sizeread);
	} while( sizeread > 0 );
	free(buffer);
	i = close(infd);
	if( i != 0 ) {
		e = errno;
		fprintf(stderr,"Error %d reading %s: %s\n",
				e, fullfilename, strerror(e));
		return RET_ERRNO(e);;
	}
	return checksums_from_context(checksums_p, &context);
}
