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

retvalue updates_getpatterns(const char *confdir,/*@out@*/struct update_pattern **patterns,int force);

void updates_freepatterns(struct update_pattern *p);
void updates_freeorigins(struct update_origin *o);
void updates_freetargets(struct update_target *t);

retvalue updates_getindices(const char *listdir,const struct update_pattern *patterns,struct distribution *distributions);

// struct aptmethodrun;
// struct downloadcache;

// retvalue updates_prepare(struct aptmethodrun *run,struct distribution *distribution);
// retvalue updates_queuelists(struct aptmethodrun *run,struct distribution *distribution,int force);
// retvalue updates_readindices(const char *dbdir,struct downloadcache *cache,filesdb filesdb,struct distribution *distribution,int force);

retvalue updates_update(const char *dbdir,const char *methoddir,filesdb filesdb,references refs,struct distribution *distributions,int force);
retvalue updates_checkupdate(const char *dbdir,const char *methoddir,struct distribution *distributions,int force);

#endif
