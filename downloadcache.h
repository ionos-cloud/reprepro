#ifndef __MIRRORER_DOWNLOADLIST_H
#define __MIRRORER_DOWNLOADLIST_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_FILES_H
#include "files.h"
#endif
#ifndef __MIRRORER_APTMETHOD_H
#include "aptmethod.h"
#endif

struct downloadcache;

/* Initialize a new download session */
retvalue downloadcache_initialize(struct downloadcache **download);

/* free all memory */
retvalue downloadcache_free(struct downloadcache *download);

/* queue a new file to be downloaded: 
 * results in RET_ERROR_WRONG_MD5, if someone else already asked
 * for the same destination with other md5sum created. */
retvalue downloadcache_add(struct downloadcache *cache,filesdb filesdb,struct aptmethod *method,const char *orig,const char *filekey,const char *md5sum);

/* some as above, only for more files... */
retvalue downloadcache_addfiles(struct downloadcache *cache,filesdb filesdb,
		struct aptmethod *method,
		const struct strlist *origfiles,
		const struct strlist *filekeys,
		const struct strlist *md5sums);
#endif
