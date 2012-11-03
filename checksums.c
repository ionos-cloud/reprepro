/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007,2008,2009 Bernhard R. Link
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
#include <stdlib.h>
#include <sys/types.h>
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
#include "configparser.h"

const char * const changes_checksum_names[] = {
	"Files", "Checksums-Sha1", "Checksums-Sha256"
};
const char * const source_checksum_names[] = {
	"Files", "Checksums-Sha1", "Checksums-Sha256"
};
const char * const release_checksum_names[cs_hashCOUNT] = {
	"MD5Sum", "SHA1", "SHA256"
};


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
#ifdef SPLINT
typedef size_t hashlen_t;
#else
typedef unsigned short hashlen_t;
#endif

struct checksums {
	struct { unsigned short ofs;
		hashlen_t len;
	} parts[cs_COUNT];
	char representation[];
};
#define checksums_hashpart(c, t) ((c)->representation + (c)->parts[t].ofs)
#define checksums_totallength(c) ((c)->parts[cs_length].ofs + (c)->parts[cs_length].len)


static const char * const hash_name[cs_COUNT] =
	{ "md5", "sha1", "sha256", "size" };

void checksums_free(struct checksums *checksums) {
	free(checksums);
}

retvalue checksums_init(/*@out@*/struct checksums **checksums_p, char *hashes[cs_COUNT]) {
	const char *p, *size;
	char *d;
	struct checksums *n;
	enum checksumtype type;
	size_t len, hashlens[cs_COUNT];

	/* everything assumes yet that this is available */
	if (hashes[cs_length] == NULL) {
		for (type = cs_md5sum ; type < cs_COUNT ; type++)
			free(hashes[type]);
		*checksums_p = NULL;
		return RET_OK;
	}

	size = hashes[cs_length];
	while (*size == '0' && size[1] >= '0' && size[1] <= '9')
		size++;

	if (hashes[cs_md5sum] == NULL)
		hashlens[cs_md5sum] = 1;
	else
		hashlens[cs_md5sum] = strlen(hashes[cs_md5sum]);
	hashlens[cs_length] = strlen(size);
	len = hashlens[cs_md5sum] + 1 + hashlens[cs_length];

	p = hashes[cs_md5sum];
	if (p != NULL) {
		while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')
				|| (*p >= 'A' && *p <= 'F'))
			p++;
		if (*p != '\0') {
			// TODO: find way to give more meaningfull error message
			fprintf(stderr, "Invalid md5 hash: '%s'\n",
					hashes[cs_md5sum]);
			for (type = cs_md5sum ; type < cs_COUNT ; type++)
				free(hashes[type]);
			return RET_ERROR;
		}
	}
	p = size;
	while ((*p >= '0' && *p <= '9'))
		p++;
	if (*p != '\0') {
		// TODO: find way to give more meaningfull error message
		fprintf(stderr, "Invalid size: '%s'\n", size);
		for (type = cs_md5sum ; type < cs_COUNT ; type++)
			free(hashes[type]);
		return RET_ERROR;
	}

	for (type = cs_firstEXTENDED ; type < cs_hashCOUNT ; type++) {
		if (hashes[type] == NULL)
			continue;
		p = hashes[type];
		while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')
				|| (*p >= 'A' && *p <= 'F'))
			p++;
		if (*p != '\0') {
			// TODO: find way to give more meaningfull error message
			fprintf(stderr, "Invalid hash: '%s'\n", hashes[type]);
			for (type = cs_md5sum ; type < cs_COUNT ; type++)
				free(hashes[type]);
			return RET_ERROR;
		}
		hashlens[type] = (size_t)(p - hashes[type]);
		len += strlen(" :x:") + hashlens[type];
	}

	n = malloc(sizeof(struct checksums) + len + 1);
	if (FAILEDTOALLOC(n)) {
		for (type = cs_md5sum ; type < cs_COUNT ; type++)
			free(hashes[type]);
		return RET_ERROR_OOM;
	}
	setzero(struct checksums, n);
	d = n->representation;

	for (type = cs_firstEXTENDED ; type < cs_hashCOUNT ; type++) {
		if (hashes[type] == NULL)
			continue;
		*(d++) = ':';
		*(d++) = '1' + (char)(type - cs_sha1sum);
		*(d++) = ':';
		n->parts[type].ofs = d - n->representation;
		n->parts[type].len = (hashlen_t)hashlens[type];
		memcpy(d, hashes[type], hashlens[type]);
		d += hashlens[type];
		*(d++) = ' ';
	}
	if (hashes[cs_md5sum] == NULL) {
		n->parts[cs_md5sum].ofs = d - n->representation;
		n->parts[cs_md5sum].len = 0;
		*(d++) = '-';
	} else {
		n->parts[cs_md5sum].ofs = d - n->representation;
		n->parts[cs_md5sum].len = (hashlen_t)hashlens[cs_md5sum];
		memcpy(d, hashes[cs_md5sum], hashlens[cs_md5sum]);
		d += hashlens[cs_md5sum];
	}
	*(d++) = ' ';
	n->parts[cs_length].ofs = d - n->representation;
	n->parts[cs_length].len = (hashlen_t)hashlens[cs_length];
	memcpy(d, size, hashlens[cs_length] + 1);
	d += hashlens[cs_length] + 1;
	assert ((size_t)(d-n->representation) == len + 1);

	for (type = cs_md5sum ; type < cs_COUNT ; type++)
		free(hashes[type]);
	*checksums_p = n;
	return RET_OK;
}

retvalue checksums_initialize(struct checksums **checksums_p, const struct hash_data *hashes) {
	char *d;
	struct checksums *n;
	enum checksumtype type;
	size_t len;

	/* everything assumes that this is available */
	if (hashes[cs_length].start == NULL) {
		assert (0 == 1);
		*checksums_p = NULL;
		return RET_ERROR;
	}

	len = hashes[cs_md5sum].len + 1 + hashes[cs_length].len;
	if (hashes[cs_md5sum].start == NULL) {
		assert(hashes[cs_md5sum].len == 0);
		len++;
	}

	for (type = cs_firstEXTENDED ; type < cs_hashCOUNT ; type++) {
		if (hashes[type].start == NULL)
			continue;
		len += strlen(" :x:") + hashes[type].len;
	}

	n = malloc(sizeof(struct checksums) + len + 1);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;

	setzero(struct checksums, n);
	d = n->representation;

	for (type = cs_firstEXTENDED ; type < cs_hashCOUNT ; type++) {
		if (hashes[type].start == NULL)
			continue;
		*(d++) = ':';
		*(d++) = '1' + (char)(type - cs_firstEXTENDED);
		*(d++) = ':';
		n->parts[type].ofs = d - n->representation;
		n->parts[type].len = (hashlen_t)hashes[type].len;
		memcpy(d, hashes[type].start, hashes[type].len);
		d += hashes[type].len;
		*(d++) = ' ';
	}
	if (hashes[cs_md5sum].start == NULL) {
		n->parts[cs_md5sum].ofs = d - n->representation;
		n->parts[cs_md5sum].len = 0;
		*(d++) = '-';
	} else {
		n->parts[cs_md5sum].ofs = d - n->representation;
		n->parts[cs_md5sum].len = (hashlen_t)hashes[cs_md5sum].len;
		memcpy(d, hashes[cs_md5sum].start, hashes[cs_md5sum].len);
		d += hashes[cs_md5sum].len;
	}
	*(d++) = ' ';
	n->parts[cs_length].ofs = d - n->representation;
	n->parts[cs_length].len = (hashlen_t)hashes[cs_length].len;
	memcpy(d, hashes[cs_length].start, hashes[cs_length].len);
	d += hashes[cs_length].len;
	*(d++) = '\0';
	assert ((size_t)(d-n->representation) == len + 1);
	*checksums_p = n;
	return RET_OK;
}

retvalue checksums_setall(/*@out@*/struct checksums **checksums_p, const char *combinedchecksum, UNUSED(size_t len)) {
	// This comes from our database, so it surely well formed
	// (as alreadyassumed above), so this should be possible to
	// do faster than that...
	return checksums_parse(checksums_p, combinedchecksum);
}

retvalue checksums_parse(struct checksums **checksums_p, const char *combinedchecksum) {
	struct checksums *n;
	size_t len = strlen(combinedchecksum);
	const char *p = combinedchecksum;
	/*@dependent@*/char *d;
	char type;
	/*@dependent@*/const char *start;

	n = malloc(sizeof(struct checksums) + len + 1);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	setzero(struct checksums, n);
	d = n->representation;
	while (*p == ':') {

		p++;
		if (p[0] == '\0' || p[1] != ':') {
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
		if (type == '1') {
			start = d;
			n->parts[cs_sha1sum].ofs = d - n->representation;
			while (*p != ' ' && *p != '\0')
				*(d++) = *(p++);
			n->parts[cs_sha1sum].len = (hashlen_t)(d - start);
		} else if (type == '2') {
			start = d;
			n->parts[cs_sha256sum].ofs = d - n->representation;
			while (*p != ' ' && *p != '\0')
				*(d++) = *(p++);
			n->parts[cs_sha256sum].len = (hashlen_t)(d - start);
		} else {
			while (*p != ' ' && *p != '\0')
				*(d++) = *(p++);
		}

		*(d++) = ' ';
		while (*p == ' ')
			p++;
	}
	n->parts[cs_md5sum].ofs = d - n->representation;
	start = d;
	if (*p == '-' && p[1] == ' ') {
		p++;
		*(d++) = '-';
		start = d;
	} else while (*p != ' ' && *p != '\0') {
		if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) {
			*(d++) = *(p++);
		} else if (*p >= 'A' && *p <= 'F') {
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
	n->parts[cs_md5sum].len = (hashlen_t)(d - start);
	*(d++) = ' ';
	while (*p == ' ')
		p++;
	n->parts[cs_length].ofs = d - n->representation;
	while (*p == '0' && (p[1] >= '0' && p[1] <= '9'))
		p++;
	start = d;
	while (*p != '\0') {
		if (*p >= '0' && *p <= '9') {
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
	n->parts[cs_length].len = (hashlen_t)(d - start);
	if (d == start) {
		// TODO: how to get some context in this?
		fprintf(stderr,
"Malformed checksums representation (no size): '%s'!\n",
				combinedchecksum);
		free(n);
		return RET_ERROR;
	}
	*d = '\0';
	assert ((size_t)(d - n->representation) <= len);
	*checksums_p = n;
	return RET_OK;
}

struct checksums *checksums_dup(const struct checksums *checksums) {
	struct checksums *n;
	size_t len;

	assert (checksums != NULL);
	len = checksums_totallength(checksums);
	assert (checksums->representation[len] == '\0');

	n = malloc(sizeof(struct checksums) + len + 1);
	if (FAILEDTOALLOC(n))
		return NULL;
	memcpy(n, checksums, sizeof(struct checksums) + len + 1);
	assert (n->representation[len] == '\0');
	return n;
}

bool checksums_getpart(const struct checksums *checksums, enum checksumtype type, const char **sum_p, size_t *size_p) {

	assert (type < cs_COUNT);

	if (checksums->parts[type].len == 0)
		return false;
	*size_p = checksums->parts[type].len;
	*sum_p = checksums_hashpart(checksums, type);
	return true;
}

bool checksums_gethashpart(const struct checksums *checksums, enum checksumtype type, const char **hash_p, size_t *hashlen_p, const char **size_p, size_t *sizelen_p) {
	assert (type < cs_hashCOUNT);
	if (checksums->parts[type].len == 0)
		return false;
	*hashlen_p = checksums->parts[type].len;
	*hash_p = checksums_hashpart(checksums, type);
	*sizelen_p = checksums->parts[cs_length].len;
	*size_p = checksums_hashpart(checksums, cs_length);
	return true;
}

retvalue checksums_getcombined(const struct checksums *checksums, /*@out@*/const char **data_p, /*@out@*/size_t *datalen_p) {
	size_t len;

	assert (checksums != NULL);
	len = checksums->parts[cs_length].ofs + checksums->parts[cs_length].len;
	assert (checksums->representation[len] == '\0');

	*data_p = checksums->representation;
	*datalen_p = len;
	return RET_OK;
}

off_t checksums_getfilesize(const struct checksums *checksums) {
	const char *p = checksums_hashpart(checksums, cs_length);
	off_t filesize;

	filesize = 0;
	while (*p <= '9' && *p >= '0') {
		filesize = filesize*10 + (size_t)(*p-'0');
		p++;
	}
	assert (*p == '\0');
	return filesize;
}

bool checksums_matches(const struct checksums *checksums, enum checksumtype type, const char *sum) {
	size_t len = (size_t)checksums->parts[type].len;

	assert (type < cs_hashCOUNT);

	if (len == 0)
		return true;

	if (strncmp(sum, checksums_hashpart(checksums, type), len) != 0)
		return false;
	if (sum[len] != ' ')
		return false;
	/* assuming count is the last part: */
	if (strncmp(sum + len + 1, checksums_hashpart(checksums, cs_length),
		   checksums->parts[cs_length].len + 1) != 0)
		return false;
	return true;
}

static inline bool differ(const struct checksums *a, const struct checksums *b, enum checksumtype type) {
	if (a->parts[type].len == 0 || b->parts[type].len == 0)
		return false;
	if (a->parts[type].len != b->parts[type].len)
		return true;
	return memcmp(checksums_hashpart(a, type),
			checksums_hashpart(b, type),
			a->parts[type].len) != 0;
}

bool checksums_check(const struct checksums *checksums, const struct checksums *realchecksums, bool *improves) {
	enum checksumtype type;
	bool additional = false;

	for (type = cs_md5sum ; type < cs_COUNT ; type++) {
		if (differ(checksums, realchecksums, type))
			return false;
		if (checksums->parts[type].len == 0 &&
		    realchecksums->parts[type].len != 0)
			additional = true;
	}
	if (improves != NULL)
		*improves = additional;
	return true;
}

void checksums_printdifferences(FILE *f, const struct checksums *expected, const struct checksums *got) {
	enum checksumtype type;

	for (type = cs_md5sum ; type < cs_COUNT ; type++) {
		if (differ(expected, got, type)) {
			fprintf(f, "%s expected: %.*s, got: %.*s\n",
					hash_name[type],
					(int)expected->parts[type].len,
					checksums_hashpart(expected, type),
					(int)got->parts[type].len,
					checksums_hashpart(got, type));
		}
	}
}

retvalue checksums_combine(struct checksums **checksums_p, const struct checksums *by, bool *improvedhashes) /*@requires only *checksums_p @*/ /*@ensures only *checksums_p @*/ {
	struct checksums *old = *checksums_p, *n;
	size_t len = checksums_totallength(old) + checksums_totallength(by);
	const char *o, *b, *start;
	char /*@dependent@*/ *d;
	char typeid;

	n = malloc(sizeof(struct checksums)+ len + 1);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	setzero(struct checksums, n);
	o = old->representation;
	b = by->representation;
	d = n->representation;
	while (*o == ':' || *b == ':') {
		if (b[0] != ':' || (o[0] == ':' && o[1] <= b[1])) {
			*(d++) = *(o++);
			typeid = *o;
			*(d++) = *(o++);
			*(d++) = *(o++);
			if (typeid == '1') {
				start = d;
				n->parts[cs_sha1sum].ofs = d - n->representation;
				while (*o != ' ' && *o != '\0')
					*(d++) = *(o++);
				n->parts[cs_sha1sum].len = (hashlen_t)(d - start);
			} else if (typeid == '2') {
				start = d;
				n->parts[cs_sha256sum].ofs = d - n->representation;
				while (*o != ' ' && *o != '\0')
					*(d++) = *(o++);
				n->parts[cs_sha256sum].len = (hashlen_t)(d - start);
			} else
				while (*o != ' ' && *o != '\0')
					*(d++) = *(o++);
			assert (*o == ' ');
			if (*o == ' ')
				*(d++) = *(o++);

			if (b[0] == ':' && typeid == b[1]) {
				while (*b != ' ' && *b != '\0')
					b++;
				assert (*b == ' ');
				if (*b == ' ')
					b++;
			}
		} else {
			*(d++) = *(b++);
			typeid = *b;
			*(d++) = *(b++);
			*(d++) = *(b++);
			if (typeid == '1') {
				if (improvedhashes != NULL)
					improvedhashes[cs_sha1sum] = true;
				start = d;
				n->parts[cs_sha1sum].ofs = d - n->representation;
				while (*b != ' ' && *b != '\0')
					*(d++) = *(b++);
				n->parts[cs_sha1sum].len = (hashlen_t)(d - start);
			} else if (typeid == '2') {
				if (improvedhashes != NULL)
					improvedhashes[cs_sha256sum] = true;
				start = d;
				n->parts[cs_sha256sum].ofs = d - n->representation;
				while (*b != ' ' && *b != '\0')
					*(d++) = *(b++);
				n->parts[cs_sha256sum].len = (hashlen_t)(d - start);
			} else
				while (*b != ' ' && *b != '\0')
					*(d++) = *(b++);
			assert (*b == ' ');
			if (*b == ' ')
				*(d++) = *(b++);
		}
	}
	/* now take md5sum from original code, unless only the new one has it */
	n->parts[cs_md5sum].ofs = d - n->representation;
	start = d;
	if (*o == '-' && *b != '-')
		o = b;
	while (*o != ' ' && *o != '\0')
		*(d++) = *(o++);
	n->parts[cs_md5sum].len = (hashlen_t)(d - start);
	assert (*o == ' ');
	if (*o == ' ')
		*(d++) = *(o++);
	/* and now the size */
	n->parts[cs_length].ofs = d - n->representation;
	start = d;
	while (*o != '\0')
		*(d++) = *(o++);
	n->parts[cs_length].len = (hashlen_t)(d - start);
	assert ((size_t)(d - n->representation) <= len);
	*(d++) = '\0';
	*checksums_p = realloc(n, sizeof(struct checksums)
	                          + (d-n->representation));
	if (*checksums_p == NULL)
		*checksums_p = n;
	checksums_free(old);
	return RET_OK;
}

void checksumsarray_done(struct checksumsarray *array) {
	if (array->names.count > 0) {
		int i;
		assert (array->checksums != NULL);
		for (i = 0 ; i < array->names.count ; i++) {
			checksums_free(array->checksums[i]);
		}
	} else
		assert (array->checksums == NULL);
	strlist_done(&array->names);
	free(array->checksums);
}

retvalue hashline_parse(const char *filenametoshow, const char *line, enum checksumtype cs, const char **basename_p, struct hash_data *data_p, struct hash_data *size_p) {
	const char *p = line;
	const char *hash_start, *size_start, *filename;
	size_t hash_len, size_len;

	while (*p == ' ' || *p == '\t')
		p++;
	hash_start = p;
	while ((*p >= '0' && *p <= '9') ||
			(*p >= 'a' && *p <= 'f'))
		p++;
	hash_len = p - hash_start;
	while (*p == ' ' || *p == '\t')
		p++;
	while (*p == '0' && p[1] >= '0' && p[1] <= '9')
		p++;
	size_start = p;
	while ((*p >= '0' && *p <= '9'))
		p++;
	size_len = p - size_start;
	while (*p == ' ' || *p == '\t')
		p++;
	filename = p;
	while (*p != '\0' && *p != ' ' && *p != '\t'
			&& *p != '\r' && *p != '\n')
		p++;
	if (unlikely(size_len == 0 || hash_len == 0
				|| filename == p || *p != '\0')) {
		fprintf(stderr,
"Error parsing %s checksum line ' %s' within '%s'\n",
				hash_name[cs], line,
				filenametoshow);
		return RET_ERROR;
	}
	*basename_p = filename;
	data_p->start = hash_start;
	data_p->len = hash_len;
	size_p->start = size_start;
	size_p->len = size_len;
	return RET_OK;
}

retvalue checksumsarray_parse(struct checksumsarray *out, const struct strlist l[cs_hashCOUNT], const char *filenametoshow) {
	retvalue r;
	int i;
	struct checksumsarray a;
	struct strlist filenames;
	size_t count;
	bool foundhashtype[cs_hashCOUNT];
	struct hashes *parsed;
	enum checksumtype cs;

	memset(foundhashtype, 0, sizeof(foundhashtype));

	/* avoid realloc by allocing the absolute maximum only
	 * if every checksum field contains different files */
	count = 0;
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		count += l[cs].count;
	}

	parsed = nzNEW(count, struct hashes);
	if (FAILEDTOALLOC(parsed))
		return RET_ERROR_OOM;
	strlist_init_n(count + 1, &filenames);
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		for (i = 0 ; i < l[cs].count ; i++) {
			const char *line = l[cs].values[i];
			const char *p = line,
			      *hash_start, *size_start, *filename;
			size_t hash_len, size_len;
			int fileofs;

			while (*p == ' ' || *p == '\t')
				p++;
			hash_start = p;
			while ((*p >= '0' && *p <= '9') ||
					(*p >= 'a' && *p <= 'f'))
				p++;
			hash_len = p - hash_start;
			while (*p == ' ' || *p == '\t')
				p++;
			while (*p == '0' && p[1] >= '0' && p[1] <= '9')
				p++;
			size_start = p;
			while ((*p >= '0' && *p <= '9'))
				p++;
			size_len = p - size_start;
			while (*p == ' ' || *p == '\t')
				p++;
			filename = p;
			while (*p != '\0' && *p != ' ' && *p != '\t'
			       && *p != '\r' && *p != '\n')
				p++;
			if (unlikely(size_len == 0 || hash_len == 0
				   || filename == p || *p != '\0')) {
				fprintf(stderr,
"Error parsing %s checksum line ' %s' within '%s'\n",
						hash_name[cs], line,
						filenametoshow);
				strlist_done(&filenames);
				free(parsed);
				return RET_ERROR;
			} else {
				struct hash_data *hashes;

				fileofs = strlist_ofs(&filenames, filename);
				if (fileofs == -1) {
					fileofs = filenames.count;
					r = strlist_add_dup(&filenames, filename);
					if (RET_WAS_ERROR(r)) {
						strlist_done(&filenames);
						free(parsed);
						return r;
					}
					hashes = parsed[fileofs].hashes;
					hashes[cs_length].start = size_start;
					hashes[cs_length].len = size_len;
				} else {
					hashes = parsed[fileofs].hashes;
					if (unlikely(hashes[cs_length].len
						      != size_len
						  || memcmp(hashes[cs_length].start,
						       size_start, size_len) != 0)) {
						fprintf(stderr,
"WARNING: %s checksum line ' %s' in '%s' contradicts previous filesize!\n",
							hash_name[cs], line,
							filenametoshow);
						continue;
					}
				}
				hashes[cs].start = hash_start;
				hashes[cs].len = hash_len;
				foundhashtype[cs] = true;
			}
		}
	}
	assert (count >= (size_t)filenames.count);

	if (filenames.count == 0) {
		strlist_done(&filenames);
		strlist_init(&out->names);
		out->checksums = NULL;
		free(parsed);
		return RET_OK;
	}
#if 0
// TODO: reenable this once apt-utils is fixed for a long enough time...
	for (i = 0 ; i < filenames.count ; i++) {
		for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
			if (!foundhashtype[cs])
				continue;
			if (parsed[i].hashes[cs].start == NULL) {
				fprintf(stderr,
"WARNING: Inconsistent hashes in %s: '%s' missing %s!\n",
						filenametoshow,
						filenames.values[i],
						hash_name[cs]);
				r = RET_ERROR;
				/* show one per file, but list all problematic files */
				break;
			}
		}
	}
#endif
	a.checksums = nzNEW(filenames.count+1, struct checksums *);
	if (FAILEDTOALLOC(a.checksums)) {
		strlist_done(&filenames);
		free(parsed);
		return RET_ERROR_OOM;
	}
	strlist_move(&a.names, &filenames);

	for (i = 0 ; i < a.names.count ; i++) {
		r = checksums_initialize(a.checksums + i, parsed[i].hashes);
		if (RET_WAS_ERROR(r)) {
			free(parsed);
			checksumsarray_done(&a);
			return r;
		}
	}
	checksumsarray_move(out, &a);
	free(parsed);
	return RET_OK;
}

retvalue checksumsarray_genfilelist(const struct checksumsarray *a, char **md5_p, char **sha1_p, char **sha256_p) {
	size_t lens[cs_hashCOUNT];
	bool missing[cs_hashCOUNT];
	char *filelines[cs_hashCOUNT];
	int i;
	enum checksumtype cs;
	size_t filenamelen[a->names.count];

	memset(missing, 0, sizeof(missing));
	memset(lens, 0, sizeof(lens));

	for (i=0 ; i < a->names.count ; i++) {
		const struct checksums *checksums = a->checksums[i];
		size_t len;

		filenamelen[i] = strlen(a->names.values[i]);

		len = 4 + filenamelen[i] + checksums->parts[cs_length].len;
		assert (checksums != NULL);
		if (checksums->parts[cs_md5sum].len == 0)
			lens[cs_md5sum] += len + 1;
		else
			lens[cs_md5sum] += len + checksums->parts[cs_md5sum].len;
		for (cs = cs_md5sum+1 ; cs < cs_hashCOUNT ; cs++) {
			if (checksums->parts[cs].len == 0)
				missing[cs] = true;
			lens[cs] += len + checksums->parts[cs].len;
		}
	}
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		if (missing[cs])
			filelines[cs] = NULL;
		else {
			filelines[cs] = malloc(lens[cs] + 1);
			if (FAILEDTOALLOC(filelines[cs])) {
				while (cs-- > cs_md5sum)
					free(filelines[cs]);
				return RET_ERROR_OOM;
			}
		}
	}
	for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
		char *p;

		if (missing[cs])
			continue;

		p = filelines[cs];
		*(p++) = '\n';
		for (i=0 ; i < a->names.count ; i++) {
			const struct checksums *c = a->checksums[i];

			*(p++) = ' ';
			if (c->parts[cs].len == 0) {
				*(p++) = '-';
			} else {
				memcpy(p, checksums_hashpart(c, cs),
						c->parts[cs].len);
				p +=  c->parts[cs].len;
			}
			*(p++) = ' ';
			memcpy(p, checksums_hashpart(c, cs_length),
					c->parts[cs_length].len);
			p +=  c->parts[cs_length].len;
			*(p++) = ' ';
			memcpy(p, a->names.values[i], filenamelen[i]);
			p += filenamelen[i];
			*(p++) = '\n';
		}
		*(--p) = '\0';
		assert ((size_t)(p - filelines[cs]) == lens[cs]);
	}
	*md5_p = filelines[cs_md5sum];
	*sha1_p = filelines[cs_sha1sum];
	*sha256_p = filelines[cs_sha256sum];
	return RET_OK;
}

void checksumsarray_move(/*@out@*/struct checksumsarray *destination, struct checksumsarray *origin) {
	strlist_move(&destination->names, &origin->names);
	destination->checksums = origin->checksums;
	origin->checksums = NULL;
}

void checksumsarray_resetunsupported(const struct checksumsarray *a, bool *types) {
	int i;
	enum checksumtype cs;

	for (i = 0 ; i < a->names.count ; i++) {
		struct checksums *c = a->checksums[i];
		for (cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++) {
			if (c->parts[cs].len == 0)
				types[cs] = false;
		}
	}
}

retvalue checksumsarray_include(struct checksumsarray *a, /*@only@*/char *name, const struct checksums *checksums) {
	retvalue r;
	struct checksums **n;
	int count = a->names.count;

	n = nNEW(count + 1, struct checksums *);
	if (FAILEDTOALLOC(n)) {
		free(name);
		return RET_ERROR_OOM;
	}
	n[0] = checksums_dup(checksums);
	if (FAILEDTOALLOC(n[0])) {
		free(name);
		free(n);
		return RET_ERROR_OOM;
	}
	r = strlist_include(&a->names, name);
	if (!RET_IS_OK(r)) {
		checksums_free(n[0]);
		free(n);
		return r;
	}
	assert (a->names.count == count + 1);
	if (count > 0) {
		assert (a->checksums != NULL);
		memcpy(&n[1], a->checksums, count*sizeof(struct checksums*));
	}
	free(a->checksums);
	a->checksums = n;
	return RET_OK;
}

/* check if the file has the given md5sum (only cheap tests like size),
 * RET_NOTHING means file does not exist, RET_ERROR_WRONG_MD5 means wrong size */
retvalue checksums_cheaptest(const char *fullfilename, const struct checksums *checksums, bool complain) {
	off_t expectedsize;
	int i;
	struct stat s;

	i = stat(fullfilename, &s);
	if (i < 0) {
		i = errno;
		if (i == EACCES || i == ENOENT)
			return RET_NOTHING;
		else {
			fprintf(stderr, "Error %d stating '%s': %s!\n",
					i, fullfilename, strerror(i));
			return RET_ERRNO(i);
		}
	}

	expectedsize = checksums_getfilesize(checksums);

	if (s.st_size == expectedsize)
		return RET_OK;
	if (complain)
		fprintf(stderr,
			"WRONG SIZE of '%s': expected %lld found %lld\n",
			fullfilename,
			(long long)expectedsize,
			(long long)s.st_size);
	return RET_ERROR_WRONG_MD5;
}

retvalue checksums_test(const char *filename, const struct checksums *checksums, struct checksums **checksums_p) {
	retvalue r;
	struct checksums *filechecksums;
	bool improves;

	/* check if it is there and has the correct size */
	r = checksums_cheaptest(filename, checksums, false);
	/* if it is, read its checksums */
	if (RET_IS_OK(r))
		r = checksums_read(filename, &filechecksums);
	if (!RET_IS_OK(r))
		return r;
	if (!checksums_check(checksums, filechecksums, &improves)) {
		checksums_free(filechecksums);
		return RET_ERROR_WRONG_MD5;
	}
	if (improves && checksums_p != NULL) {
		if (*checksums_p == NULL) {
			*checksums_p = checksums_dup(checksums);
			if (FAILEDTOALLOC(*checksums_p)) {
				checksums_free(filechecksums);
				return RET_ERROR_OOM;
			}
		}
		r = checksums_combine(checksums_p, filechecksums, NULL);
		if (RET_WAS_ERROR(r)) {
			checksums_free(filechecksums);
			return r;
		}
	}
	checksums_free(filechecksums);
	return RET_OK;
}

/* copy, only checking file size, perhaps add some paranoia checks later */
static retvalue copy(const char *destination, const char *source, const struct checksums *checksums) {
	off_t filesize = 0, expected;
	static const size_t bufsize = 16384;
	char *buffer = malloc(bufsize);
	ssize_t sizeread, towrite, written;
	const char *start;
	int e, i;
	int infd, outfd;

	if (FAILEDTOALLOC(buffer))
		return RET_ERROR_OOM;

	infd = open(source, O_RDONLY);
	if (infd < 0) {
		e = errno;
		fprintf(stderr, "Error %d opening '%s': %s\n",
				e, source, strerror(e));
		free(buffer);
		return RET_ERRNO(e);
	}
	outfd = open(destination, O_NOCTTY|O_WRONLY|O_CREAT|O_EXCL, 0666);
	if (outfd < 0) {
		e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, destination, strerror(e));
		(void)close(infd);
		free(buffer);
		return RET_ERRNO(e);
	}
	filesize = 0;
	do {
		sizeread = read(infd, buffer, bufsize);
		if (sizeread < 0) {
			e = errno;
			fprintf(stderr, "Error %d while reading %s: %s\n",
					e, source, strerror(e));
			free(buffer);
			(void)close(infd); (void)close(outfd);
			deletefile(destination);
			return RET_ERRNO(e);;
		}
		filesize += sizeread;
		towrite = sizeread;
		start = buffer;
		while (towrite > 0) {
			written = write(outfd, start, (size_t)towrite);
			if (written < 0) {
				e = errno;
				fprintf(stderr,
"Error %d while writing to %s: %s\n",
						e, destination, strerror(e));
				free(buffer);
				(void)close(infd); (void)close(outfd);
				deletefile(destination);
				return RET_ERRNO(e);;
			}
			towrite -= written;
			start += written;
		}
	} while (sizeread > 0);
	free(buffer);
	i = close(infd);
	if (i != 0) {
		e = errno;
		fprintf(stderr, "Error %d reading %s: %s\n",
				e, source, strerror(e));
		(void)close(outfd);
		deletefile(destination);
		return RET_ERRNO(e);;
	}
	i = close(outfd);
	if (i != 0) {
		e = errno;
		fprintf(stderr, "Error %d writing to %s: %s\n",
				e, destination, strerror(e));
		deletefile(destination);
		return RET_ERRNO(e);;
	}
	expected = checksums_getfilesize(checksums);
	if (filesize != expected) {
		fprintf(stderr,
"Error copying %s to %s:\n"
" File seems to be of size %llu, while %llu was expected!\n",
				source, destination,
				(unsigned long long)filesize,
				(unsigned long long)expected);
		deletefile(destination);
		return RET_ERROR_WRONG_MD5;
	}
	return RET_OK;
}

retvalue checksums_hardlink(const char *directory, const char *filekey, const char *sourcefilename, const struct checksums *checksums) {
	retvalue r;
	int i, e;
	char *fullfilename = calc_dirconcat(directory, filekey);
	if (FAILEDTOALLOC(fullfilename))
		return RET_ERROR_OOM;

	i = link(sourcefilename, fullfilename);
	e = errno;
	if (i != 0 && e == EEXIST)  {
		(void)unlink(fullfilename);
		errno = 0;
		i = link(sourcefilename, fullfilename);
		e = errno;
	}
	if (i != 0 && (e == EACCES || e == ENOENT || e == ENOTDIR))  {
		errno = 0;
		(void)dirs_make_parent(fullfilename);
		i = link(sourcefilename, fullfilename);
		e = errno;
	}
	if (i != 0) {
		if (e == EXDEV || e == EPERM || e == EMLINK) {
			r = copy(fullfilename, sourcefilename, checksums);
			if (RET_WAS_ERROR(r)) {
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
	SHA256Init(&context->sha256);
}

void checksumscontext_update(struct checksumscontext *context, const unsigned char *data, size_t len) {
	MD5Update(&context->md5, data, len);
// TODO: sha1 and sha256 share quite some stuff,
// the code can most likely be combined with quite some synergies..
	SHA1Update(&context->sha1, data, len);
	SHA256Update(&context->sha256, data, len);
}

static const char tab[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

retvalue checksums_from_context(struct checksums **out, struct checksumscontext *context) {
	unsigned char md5buffer[MD5_DIGEST_SIZE], sha1buffer[SHA1_DIGEST_SIZE],
		      sha256buffer[SHA256_DIGEST_SIZE];
	char *d;
	unsigned int i;
	struct checksums *n;

	n = malloc(sizeof(struct checksums) + 2*MD5_DIGEST_SIZE
			+ 2*SHA1_DIGEST_SIZE + 2*SHA256_DIGEST_SIZE + 30);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	setzero(struct checksums, n);
	d = n->representation;
	*(d++) = ':';
	*(d++) = '1';
	*(d++) = ':';
	n->parts[cs_sha1sum].ofs = 3;
	n->parts[cs_sha1sum].len = 2*SHA1_DIGEST_SIZE;
	SHA1Final(&context->sha1, sha1buffer);
	for (i = 0 ; i < SHA1_DIGEST_SIZE ; i++) {
		*(d++) = tab[sha1buffer[i] >> 4];
		*(d++) = tab[sha1buffer[i] & 0xF];
	}
	*(d++) = ' ';

	*(d++) = ':';
	*(d++) = '2';
	*(d++) = ':';
	n->parts[cs_sha256sum].ofs = d - n->representation;
	n->parts[cs_sha256sum].len = 2*SHA256_DIGEST_SIZE;
	SHA256Final(&context->sha256, sha256buffer);
	for (i = 0 ; i < SHA256_DIGEST_SIZE ; i++) {
		*(d++) = tab[sha256buffer[i] >> 4];
		*(d++) = tab[sha256buffer[i] & 0xF];
	}
	*(d++) = ' ';

	n->parts[cs_md5sum].ofs = d - n->representation;
	assert (d - n->representation == n->parts[cs_md5sum].ofs);
	n->parts[cs_md5sum].len = 2*MD5_DIGEST_SIZE;
	MD5Final(md5buffer, &context->md5);
	for (i=0 ; i < MD5_DIGEST_SIZE ; i++) {
		*(d++) = tab[md5buffer[i] >> 4];
		*(d++) = tab[md5buffer[i] & 0xF];
	}
	*(d++) = ' ';
	n->parts[cs_length].ofs = d - n->representation;
	assert (d - n->representation == n->parts[cs_length].ofs);
	n->parts[cs_length].len = (hashlen_t)snprintf(d,
			2*MD5_DIGEST_SIZE + 2*SHA1_DIGEST_SIZE
			+ 2*SHA256_DIGEST_SIZE + 30
			- (d - n->representation), "%lld",
			(long long)context->sha1.count);
	assert (strlen(d) == n->parts[cs_length].len);
	*out = n;
	return RET_OK;
}

bool checksums_iscomplete(const struct checksums *checksums) {
	return checksums->parts[cs_md5sum].len != 0 &&
	    checksums->parts[cs_sha1sum].len != 0 &&
	    checksums->parts[cs_sha256sum].len != 0;
}

/* Collect missing checksums.
 * if the file is not there, return RET_NOTHING.
 * return RET_ERROR_WRONG_MD5 if already existing do not match */
retvalue checksums_complete(struct checksums **checksums_p, const char *fullfilename) {
	if (checksums_iscomplete(*checksums_p))
		return RET_OK;
	return checksums_test(fullfilename, *checksums_p, checksums_p);
}

retvalue checksums_read(const char *fullfilename, /*@out@*/struct checksums **checksums_p) {
	struct checksumscontext context;
	static const size_t bufsize = 16384;
	unsigned char *buffer = malloc(bufsize);
	ssize_t sizeread;
	int e, i;
	int infd;

	if (FAILEDTOALLOC(buffer))
		return RET_ERROR_OOM;

	checksumscontext_init(&context);

	infd = open(fullfilename, O_RDONLY);
	if (infd < 0) {
		e = errno;
		if ((e == EACCES || e == ENOENT) &&
				!isregularfile(fullfilename)) {
			free(buffer);
			return RET_NOTHING;
		}
		fprintf(stderr, "Error %d opening '%s': %s\n",
				e, fullfilename, strerror(e));
		free(buffer);
		return RET_ERRNO(e);
	}
	do {
		sizeread = read(infd, buffer, bufsize);
		if (sizeread < 0) {
			e = errno;
			fprintf(stderr, "Error %d while reading %s: %s\n",
					e, fullfilename, strerror(e));
			free(buffer);
			(void)close(infd);
			return RET_ERRNO(e);;
		}
		checksumscontext_update(&context, buffer, (size_t)sizeread);
	} while (sizeread > 0);
	free(buffer);
	i = close(infd);
	if (i != 0) {
		e = errno;
		fprintf(stderr, "Error %d reading %s: %s\n",
				e, fullfilename, strerror(e));
		return RET_ERRNO(e);;
	}
	return checksums_from_context(checksums_p, &context);
}

retvalue checksums_copyfile(const char *destination, const char *source, bool deletetarget, struct checksums **checksums_p) {
	struct checksumscontext context;
	static const size_t bufsize = 16384;
	unsigned char *buffer = malloc(bufsize);
	ssize_t sizeread, towrite, written;
	const unsigned char *start;
	int e, i;
	int infd, outfd;

	if (FAILEDTOALLOC(buffer))
		return RET_ERROR_OOM;

	infd = open(source, O_RDONLY);
	if (infd < 0) {
		e = errno;
		fprintf(stderr, "Error %d opening '%s': %s\n",
				e, source, strerror(e));
		free(buffer);
		return RET_ERRNO(e);
	}
	outfd = open(destination, O_NOCTTY|O_WRONLY|O_CREAT|O_EXCL, 0666);
	if (outfd < 0) {
		e = errno;
		if (e == EEXIST) {
			if (deletetarget) {
				i = unlink(destination);
				if (i != 0) {
					e = errno;
					fprintf(stderr,
"Error %d deleting '%s': %s\n",
						e, destination, strerror(e));
					(void)close(infd);
					free(buffer);
					return RET_ERRNO(e);
				}
				outfd = open(destination,
					O_NOCTTY|O_WRONLY|O_CREAT|O_EXCL,
					0666);
				e = errno;
			} else {
				(void)close(infd);
				free(buffer);
				return RET_ERROR_EXIST;
			}
		}
		if (outfd < 0) {
			fprintf(stderr,
"Error %d creating '%s': %s\n",
					e, destination, strerror(e));
			(void)close(infd);
			free(buffer);
			return RET_ERRNO(e);
		}
	}
	checksumscontext_init(&context);
	do {
		sizeread = read(infd, buffer, bufsize);
		if (sizeread < 0) {
			e = errno;
			fprintf(stderr, "Error %d while reading %s: %s\n",
					e, source, strerror(e));
			free(buffer);
			(void)close(infd); (void)close(outfd);
			deletefile(destination);
			return RET_ERRNO(e);;
		}
		checksumscontext_update(&context, buffer, (size_t)sizeread);
		towrite = sizeread;
		start = buffer;
		while (towrite > 0) {
			written = write(outfd, start, (size_t)towrite);
			if (written < 0) {
				e = errno;
				fprintf(stderr,
"Error %d while writing to %s: %s\n",
						e, destination, strerror(e));
				free(buffer);
				(void)close(infd); (void)close(outfd);
				deletefile(destination);
				return RET_ERRNO(e);;
			}
			towrite -= written;
			start += written;
		}
	} while (sizeread > 0);
	free(buffer);
	i = close(infd);
	if (i != 0) {
		e = errno;
		fprintf(stderr, "Error %d reading %s: %s\n",
				e, source, strerror(e));
		(void)close(outfd);
		deletefile(destination);
		return RET_ERRNO(e);;
	}
	i = close(outfd);
	if (i != 0) {
		e = errno;
		fprintf(stderr, "Error %d writing to %s: %s\n",
				e, destination, strerror(e));
		deletefile(destination);
		return RET_ERRNO(e);;
	}
	return checksums_from_context(checksums_p, &context);
}

retvalue checksums_linkorcopyfile(const char *destination, const char *source, struct checksums **checksums_p) {
	int i;
	retvalue r;

	// TODO: is this needed? perhaps move this duty to the caller...
	r = dirs_make_parent(destination);
	if (RET_WAS_ERROR(r))
		return r;

	errno = 0;
	i = link(source, destination);
	if (i != 0)
		return checksums_copyfile(destination, source, true, checksums_p);
	*checksums_p = NULL;
	return RET_OK;
}

retvalue checksums_replace(const char *filename, const char *data, size_t len, struct checksums **checksums_p){
	struct checksumscontext context;
	size_t todo; const char *towrite;
	char *tempfilename;
	struct checksums *checksums;
	int fd, ret;
	retvalue r;

	tempfilename = calc_addsuffix(filename, "new");
	if (FAILEDTOALLOC(tempfilename))
		return RET_ERROR_OOM;

	fd = open(tempfilename, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
	if (fd < 0) {
		int e = errno;
		fprintf(stderr, "ERROR creating '%s': %s\n", tempfilename,
				strerror(e));
		free(tempfilename);
		return RET_ERRNO(e);
	}

	todo = len; towrite = data;
	while (todo > 0) {
		ssize_t written = write(fd, towrite, todo);
		if (written >= 0) {
			todo -= written;
			towrite += written;
		} else {
			int e = errno;
			close(fd);
			fprintf(stderr, "Error writing to '%s': %s\n",
					tempfilename, strerror(e));
			unlink(tempfilename);
			free(tempfilename);
			return RET_ERRNO(e);
		}
	}
	ret = close(fd);
	if (ret < 0) {
		int e = errno;
		fprintf(stderr, "Error writing to '%s': %s\n",
				tempfilename, strerror(e));
		unlink(tempfilename);
		free(tempfilename);
		return RET_ERRNO(e);
	}

	if (checksums_p != NULL) {
		checksumscontext_init(&context);
		checksumscontext_update(&context, (const unsigned char *)data, len);
		r = checksums_from_context(&checksums, &context);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r)) {
			unlink(tempfilename);
			free(tempfilename);
			return r;
		}
	} else
		checksums = NULL;
	ret = rename(tempfilename, filename);
	if (ret < 0) {
		int e = errno;
		fprintf(stderr, "Error moving '%s' to '%s': %s\n",
				tempfilename, filename,  strerror(e));
		unlink(tempfilename);
		free(tempfilename);
		checksums_free(checksums);
		return RET_ERRNO(e);
	}
	free(tempfilename);
	if (checksums_p != NULL)
		*checksums_p = checksums;
	return RET_OK;
}

const struct constant hashes_constants[cs_hashCOUNT+1] = {
	{"md5", cs_md5sum},
	{"sha1", cs_sha1sum},
	{"sha256", cs_sha256sum},
	{NULL, 0}
}, *hashnames = hashes_constants;
