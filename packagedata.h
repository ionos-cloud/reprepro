#ifndef REPREPRO_PACKAGEDATA_H
#define REPREPRO_PACKAGEDATA_H

struct packagedata {
	char *chunk;
};

// Free dynamic data structures inside struct packagedata.
static inline void packagedata_free(struct packagedata *packagedata) {
	free(packagedata->chunk);
	setzero(struct packagedata, packagedata);
}

retvalue parse_packagedata(const char *data, /*@out@*/struct packagedata *packagedata);

#endif
