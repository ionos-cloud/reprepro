#ifndef REPREPRO_UPDATES_H
#define REPREPRO_UPDATES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

#include "release.h"
#include "strlist.h"

struct update_pattern;
struct update_origin;
struct update_target;

retvalue updates_getpatterns(const char *confdir,/*@out@*/struct update_pattern **patterns);

void updates_freepatterns(struct update_pattern *p);
void updates_freeorigins(struct update_origin *o);
void updates_freetargets(struct update_target *t);

retvalue updates_calcindices(const char *listdir,const struct update_pattern *patterns,struct distribution *distributions);

/* remove all files ${listdir}/${distribution}_* that will not be needed. */
retvalue updates_clearlists(const char *listdir,struct distribution *distributions);

retvalue updates_update(const char *dbdir,const char *methoddir,filesdb filesdb,references refs,struct distribution *distributions,int force,bool_t nolistdownload,struct strlist *dereferencedfilekeys);
retvalue updates_checkupdate(const char *dbdir,const char *methoddir,struct distribution *distributions,int force,bool_t nolistdownload);

#endif
