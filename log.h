#ifndef REPREPRO_LOG_H
#define REPREPRO_LOG_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_HOOKS_H
#include "hooks.h"
#endif

struct target;
struct logger;

retvalue logger_init(struct configiterator *, /*@out@*/struct logger **);
void logger_free(/*@only@*/struct logger *);

retvalue logger_prepare(struct logger *logger);
bool logger_isprepared(/*@null@*/const struct logger *logger);

void logger_logchanges(struct logger *, const char * /*codename*/, const char * /*name*/, const char * /*version*/, const char * /*safefilename*/, /*@null@*/const char * /*changesfilekey*/);

void logger_log(struct logger *, struct target *, const char * /*name*/, /*@null@*/const char * /*version*/, /*@null@*/const char */*oldversion*/, /*@null@*/const struct strlist * /*filekeys*/, /*@null@*/const struct strlist * /*oldfilekeys*/, /*@null@*/const char * /*causingrule*/, /*@null@*/const char * /*suitefrom*/);

bool logger_rerun_needs_target(const struct logger *, const struct target *);
retvalue logger_reruninfo(struct logger *, struct target *, const char * /*name*/, const char * /*version*/, /*@null@*/const struct strlist * /*filekeys*/);

/* wait for all jobs to finish */
void logger_wait(void);
void logger_warn_waiting(void);
#endif
