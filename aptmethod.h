#ifndef REPREPRO_APTMETHOD_H
#define REPREPRO_APTMETHOD_H

#ifndef REPREPRO_FILES_H
#include "files.h"
#endif

struct aptmethodrun;
struct aptmethod;

struct tobedone {
	struct tobedone *next;
	/* must be saved to know where is should be moved to: */
	char *uri;
	char *filename;
	/* if non-NULL, what is expected...*/
	/*@null@*/char *md5sum;
	/* if non-NULL, add to the database after found (only if md5sum != NULL) */
	/*@null@*/char *filekey;
};

retvalue aptmethod_initialize_run(struct aptmethodrun **run);
retvalue aptmethod_newmethod(struct aptmethodrun *run,const char *uri,const char *config,struct aptmethod **m);

/* md5sum can be NULL(filekey then, too): if todo != NULL, then *todo will be set */
retvalue aptmethod_queuefile(struct aptmethod *method,const char *origfile,const char *destfile,const char *md5sum,const char *filekey,struct tobedone **todo);
retvalue aptmethod_queueindexfile(struct aptmethod *method,const char *origfile,const char *destfile);

retvalue aptmethod_download(struct aptmethodrun *run,const char *methoddir,filesdb filesdb);
retvalue aptmethod_shutdown(/*only*/struct aptmethodrun *run);

#endif
