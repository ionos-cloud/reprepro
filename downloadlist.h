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

struct download_upstream;
struct downloadlist;

/* Initialize a new download session */
retvalue downloadlist_initialize(struct downloadlist **download,const char *dbdir,const char *pooldir);
filesdb downloadlist_filesdb(struct downloadlist *list);

/* free all memory, cancel all queued downloads */
retvalue downloadlist_free(struct downloadlist *downloadlist);

/* try to fetch and register all queued files */
retvalue downloadlist_run(struct downloadlist *list,const char *methodir,int force);

/* add a new origin to download files from */
retvalue downloadlist_newupstream(struct downloadlist *list,
		const char *method,const char *config,
		struct download_upstream **upstream);
		
/* queue a new file to be downloaded: 
 * results in RET_ERROR_WRONG_MD5, if someone else already asked
 * for the same destination with other md5sum created. */
retvalue downloadlist_add(struct download_upstream *upstream,const char *orig,const char *filekey,const char *md5sum);

/* some as above, only for more files... */
retvalue downloadlist_addfiles(struct download_upstream *upstream,
		const struct strlist *origfiles,
		const struct strlist *filekeys,
		const struct strlist *md5sums);
#endif
