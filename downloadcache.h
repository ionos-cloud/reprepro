#ifndef REPREPRO_DOWNLOADLIST_H
#define REPREPRO_DOWNLOADLIST_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_APTMETHOD_H
#include "aptmethod.h"
#endif
#ifndef REPREPRO_CHECKSUMS_H
#include "checksums.h"
#endif
#ifndef REPREPRO_FREESPACE_H
#include "freespace.h"
#endif

struct downloaditem;

struct downloadcache {
	/*@null@*/struct downloaditem *items;
	/*@null@*/struct devices *devices;

	/* for showing what percentage was downloaded */
	long long size_todo, size_done;
	unsigned int last_percent;
};

/* Initialize a new download session */
retvalue downloadcache_initialize(enum spacecheckmode, off_t /*reserveddb*/, off_t /*reservedother*/, /*@out@*/struct downloadcache **);

/* free all memory */
retvalue downloadcache_free(/*@null@*//*@only@*/struct downloadcache *);

/* queue a new file to be downloaded:
 * results in RET_ERROR_WRONG_MD5, if someone else already asked
 * for the same destination with other md5sum created. */
retvalue downloadcache_add(struct downloadcache *, struct aptmethod *, const char * /*orig*/, const char * /*filekey*/, const struct checksums *);

/* some as above, only for more files... */
retvalue downloadcache_addfiles(struct downloadcache *, struct aptmethod *, const struct checksumsarray * /*origfiles*/, const struct strlist * /*filekeys*/);
#endif
