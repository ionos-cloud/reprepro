#ifndef REPREPRO_DONEFILE_H
#define REPREPRO_DONEFILE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif

struct checksums;
retvalue donefile_isold(const char *filename, const struct checksums *expected);
retvalue donefile_create(const char *filename, const struct checksums *);
void donefile_delete(const char *filename);

#endif
