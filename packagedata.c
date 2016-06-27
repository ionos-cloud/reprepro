#include <config.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "error.h"
#include "packagedata.h"

// The return structure packagedata must already be allocated.
// A datablock is allocated on success and packagedata_free() needs to be called to free it.
retvalue packagedata_create(const char *version, const char *controlchunk, /*@out@*/struct packagedata *packagedata) {
	size_t version_len;
	size_t controlchunk_len;
	retvalue result;

	assert (packagedata != NULL);

	version_len = strlen(version) + 1;
	controlchunk_len = strlen(controlchunk) + 1;
	packagedata->data_len = version_len + controlchunk_len + sizeof(int64_t) +
			sizeof(struct fields_len);
	packagedata->data = malloc(packagedata->data_len);
	if (likely(packagedata->data != NULL)) {
		packagedata->version = (char*)packagedata->data;
		packagedata->chunk = (char*)((size_t)packagedata->data + version_len);
		packagedata->added = (int64_t*)((size_t)packagedata->chunk + controlchunk_len);
		packagedata->fields_len = (struct fields_len*)((size_t)packagedata->data + packagedata->data_len) - 1;

		memcpy(packagedata->version, version, version_len);
		memcpy(packagedata->chunk, controlchunk, controlchunk_len);
		*packagedata->added = time(NULL);
		packagedata->fields_len->chunk_len = controlchunk_len;
		packagedata->fields_len->version_len = version_len;
		packagedata->fields_len->number_of_fields = 3;
		result = RET_OK;
	} else {
		setzero(struct packagedata, packagedata);
		result = RET_ERROR_OOM;
	}
	return result;
}

// The return structure packagedata must already be allocated.
retvalue parse_packagedata(void *data, const size_t data_len, /*@out@*/struct packagedata *packagedata) {
	size_t expected_len;

	assert (packagedata != NULL);

	packagedata->data = data;
	packagedata->data_len = data_len;

	if (unlikely(data_len < sizeof(struct fields_len))) {
		fprintf(stderr, "Database returned corrupted (too small) data (%zu < %zu)!\n",
		        data_len, sizeof(struct fields_len));
		return RET_ERROR;
	}
	packagedata->fields_len = (struct fields_len*)((size_t)packagedata->data + packagedata->data_len) - 1;
	packagedata->version = (char*)packagedata->data;
	packagedata->chunk = (char*)((size_t)packagedata->data + packagedata->fields_len->version_len);
	packagedata->added = (int64_t*)((size_t)packagedata->chunk + packagedata->fields_len->chunk_len);

	expected_len = packagedata->fields_len->version_len + packagedata->fields_len->chunk_len +
			sizeof(int64_t) + sizeof(struct fields_len);
	if (unlikely(data_len < expected_len)) {
		fprintf(stderr, "Database returned corrupted (too small) data (%zu < %zu)!\n",
		        data_len, expected_len);
		return RET_ERROR;
	} else if (unlikely(packagedata->fields_len->number_of_fields < 3)) {
		fprintf(stderr, "Database returned data with %i fields, but at least 3 are required!",
		        packagedata->fields_len->number_of_fields);
		return RET_ERROR;
	} else if (unlikely(packagedata->version[packagedata->fields_len->version_len-1] != '\0' ||
	                    packagedata->chunk[packagedata->fields_len->chunk_len-1] != '\0')) {
		fprintf(stderr, "Database returned corrupted (not null-terminated) strings!");
		return RET_ERROR;
	}
	return RET_OK;
}
