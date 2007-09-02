#ifndef REPREPRO_LOG_H
#define REPREPRO_LOG_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

struct target;
struct logger;

enum log_action { LOG_PACKAGE_ADD, LOG_PACKAGE_REPLACE, LOG_PACKAGE_REMOVE};

retvalue logger_init(const char *confdir,const char *logdir,struct configiterator *,/*@out@*/struct logger **);
void logger_free(/*@only@*/struct logger *);

retvalue logger_prepare(struct logger *logger);
bool_t logger_isprepared(/*@null@*/const struct logger *logger);

void logger_logchanges(struct logger *,const char *codename,const char *name,const char *version,const char *data,const char *safefilename,/*@null@*/const char *changesfilekey);

void logger_log(struct logger *,struct target *,const char *name,/*@null@*/const char *version,/*@null@*/const char *oldversion,/*@null@*/const char *control,/*@null@*/const char *oldcontrol,/*@null@*/const struct strlist *filekeys,/*@null@*/const struct strlist *oldfilekeys);

bool_t logger_rerun_needs_target(const struct logger *,const struct target *);
retvalue logger_reruninfo(struct logger *,struct target *,const char *name,const char *version,const char *control,/*@null@*/const struct strlist *filekeys);

/* do work that is left */
retvalue logger_continue(struct logger *);
/* wait for all jobs to finish */
void logger_wait(void);
void logger_warn_waiting(void);
#endif
