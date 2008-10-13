#ifndef REPREPRO_POOL_H
#define REPREPRO_POOL_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

extern bool pool_havedereferenced;

retvalue pool_dereferenced(const char *);
retvalue pool_removeunreferenced(struct database *);

#endif
