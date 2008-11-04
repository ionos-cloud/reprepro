#ifndef REPREPRO_POOL_H
#define REPREPRO_POOL_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

extern bool pool_havedereferenced;

/* called from references.c to note the file lost a reference */
retvalue pool_dereferenced(const char *);
/* called from files.c to note the file was added or forgotten */
retvalue pool_markadded(const char *);
retvalue pool_markdeleted(const char *);

/* Remove all files that lost their last reference, or only count them */
retvalue pool_removeunreferenced(struct database *, bool delete);

/* Delete all added files that are not used, or only count them */
void pool_tidyadded(struct database*, bool deletenew);

/* delete and forget a single file */
retvalue pool_delete(struct database *, const char *);

#endif
