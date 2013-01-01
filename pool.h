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
retvalue pool_removeunreferenced(bool /*delete*/);

/* Delete all added files that are not used, or only count them */
void pool_tidyadded(bool deletenew);

/* delete and forget a single file */
retvalue pool_delete(const char *);

/* notify outhook of new files */
void pool_sendnewfiles(void);

/* free all memory, to make valgrind happier */
void pool_free(void);
#endif
