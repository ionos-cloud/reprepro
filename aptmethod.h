#ifndef REPREPRO_APTMETHOD_H
#define REPREPRO_APTMETHOD_H

#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_CHECKSUMS_H
#include "checksums.h"
#endif

struct aptmethodrun;
struct aptmethod;

enum queue_action { qa_abort, qa_got, qa_error };

typedef retvalue queue_callback(enum queue_action, void *, void *, const char * /*uri*/, const char * /*gotfilename*/, const char * /*wantedfilename*/, /*@null@*/const struct checksums *, const char * /*methodname*/);

retvalue aptmethod_initialize_run(/*@out@*/struct aptmethodrun **);
retvalue aptmethod_newmethod(struct aptmethodrun *, const char * /*uri*/, const char * /*fallbackuri*/, const struct strlist * /*config*/, /*@out@*/struct aptmethod **);

retvalue aptmethod_enqueue(struct aptmethod *, const char * /*origfile*/, /*@only@*/char */*destfile*/, queue_callback *, void *, void *);
retvalue aptmethod_enqueueindex(struct aptmethod *, const char * /*suite*/, const char * /*origfile*/, const char *, const char * /*destfile*/, const char *, queue_callback *, void *, void *);

retvalue aptmethod_download(struct aptmethodrun *);
retvalue aptmethod_shutdown(/*@only@*/struct aptmethodrun *);

#endif
