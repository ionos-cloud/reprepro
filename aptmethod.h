#ifndef __MIRRORER_APTMETHOD_H
#define __MIRRORER_APTMETHOD_H

#ifndef __MIRRORER_FILES_H
#include "files.h"
#endif

struct aptmethodrun;
struct aptmethod;

retvalue aptmethod_initialize_run(struct aptmethodrun **run);
retvalue aptmethod_newmethod(struct aptmethodrun *run,const char *uri,const char *config,struct aptmethod **m);
/* md5sum can be NULL: */
retvalue aptmethod_queuefile(struct aptmethod *method,const char *origfile,const char *destfile,const char *md5sum,const char *filekey);

retvalue aptmethod_download(struct aptmethodrun *run,const char *methoddir,filesdb filesdb);
retvalue aptmethod_shutdown(struct aptmethodrun *run);

#endif
