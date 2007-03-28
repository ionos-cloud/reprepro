#ifndef REPREPRO_LOG_H
#define REPREPRO_LOG_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

struct target;
struct logger;

enum log_action { LOG_PACKAGE_ADD, LOG_PACKAGE_REPLACE, LOG_PACKAGE_REMOVE};

retvalue logger_init(const char *confdir,const char *logdir,const char *codename,const char *option,/*@null@*/const struct strlist *notifiers,/*@out@*/struct logger **);
void logger_free(/*@only@*/struct logger *);

retvalue logger_prepare(struct logger *logger);
bool_t logger_isprepared(/*@null@*/const struct logger *logger);

void logger_log(struct logger *,struct target *,const char *name,/*@null@*/const char *version,/*@null@*/const char *oldversion,/*@null@*/const char *control,/*@null@*/const char *oldcontrol,/*@null@*/const struct strlist *filekeys,/*@null@*/const struct strlist *oldfilekeys);

/* do work that is left */
retvalue logger_continue(struct logger*);
/* wait for all jobs to finish */
void logger_wait(void);

#endif
