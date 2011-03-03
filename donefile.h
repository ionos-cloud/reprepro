#ifndef REPREPRO_DONEFILE_H
#define REPREPRO_DONEFILE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif

retvalue donefile_isold(const char *filename, const char *expected);
retvalue donefile_create(const char *filename, const char *md5sum);
void donefile_delete(const char *filename);

#endif
