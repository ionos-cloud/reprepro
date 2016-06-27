#ifndef REPREPRO_PACKAGEDATA_H
#define REPREPRO_PACKAGEDATA_H

#include <stdint.h>

#include "error.h"

struct __attribute__((__packed__)) fields_len {
	uint32_t chunk_len;
	uint32_t version_len;
	int8_t number_of_fields;
};

/* On-disk format of the package data:
 *
 * (variable length) version string with ending '\0'
 * (variable length) control chunk string with ending '\0'
 * int64_t added timestamp
 * uint32_t chunk_len
 * uint32_t version_len
 * int8_t number_of_fields = 3
 */
struct packagedata {
	// data points to continuous memory block containing all fields (similar to a struct)
	void *data;
	size_t data_len;
	// The following fields point into the data memory block
	char *version;
	char *chunk;
	int64_t *added;
	struct fields_len *fields_len;
};

// Free dynamic data structures inside struct packagedata.
static inline void packagedata_free(struct packagedata *packagedata) {
	free(packagedata->data);
	setzero(struct packagedata, packagedata);
}

static inline char *packagedata_primarykey(const char *packagename, const char *version) {
	char *key;

	assert (packagename != NULL);
	assert (version != NULL);
	key = malloc(strlen(packagename) + 1 + strlen(version) + 1);
	if (key != NULL) {
		strcpy(key, packagename);
		strcat(key, "|");
		strcat(key, version);
	}
	return key;
}

retvalue packagedata_create(const char *version, const char *controlchunk, /*@out@*/struct packagedata *packagedata);
retvalue parse_packagedata(void *data, const size_t data_len, /*@out@*/struct packagedata *packagedata);

#endif
