#ifndef __MIRRORER_APTMETHOD_H
#define __MIRRORER_APTMETHOD_H

struct aptmethodrun;
struct aptmethod;

retvalue aptmethod_initialize_run(struct aptmethodrun **run);
retvalue aptmethod_newmethod(struct aptmethodrun *run,const char *uri,const char *config,struct aptmethod **m);
/* md5sum can be NULL: */
retvalue aptmethod_queuefile(struct aptmethod *method,const char *origfile,const char *destfile,const char *md5sum);

retvalue aptmethod_download(struct aptmethodrun *run,const char *methoddir);
void aptmethod_cancel(struct aptmethodrun *run);

#endif
