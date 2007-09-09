#ifndef REPREPRO_APTMETHOD_H
#define REPREPRO_APTMETHOD_H

#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

struct aptmethodrun;
struct aptmethod;

struct tobedone {
	/*@null@*/
	struct tobedone *next;
	/* must be saved to know where is should be moved to: */
	/*@notnull@*/
	char *uri;
	/*@notnull@*/
	char *filename;
	/* if non-NULL, what is expected...*/
	/*@null@*/
	char *md5sum;
	/* if non-NULL, add to the database after found (only if md5sum != NULL) */
	/*@null@*/
	char *filekey;
};

retvalue aptmethod_initialize_run(/*@out@*/struct aptmethodrun **run);
retvalue aptmethod_newmethod(struct aptmethodrun *run,const char *uri,const char *fallbackuri,const char *config,/*@out@*/struct aptmethod **m);

/* md5sum can be NULL(filekey then, too): if todo != NULL, then *todo will be set */
retvalue aptmethod_queuefile(struct aptmethod *, const char *origfile, const char *destfile, const char *md5sum, const char *filekey, /*@out@*/struct tobedone **);
retvalue aptmethod_queueindexfile(struct aptmethod *, const char *origfile, const char *destfile, /*@null@*/const char *md5sum);

retvalue aptmethod_download(struct aptmethodrun *run,const char *methoddir,struct database *);
retvalue aptmethod_shutdown(/*@only@*/struct aptmethodrun *run);

#endif
