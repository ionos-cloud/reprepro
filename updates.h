#ifndef __MIRRORER_UPDATES_H
#define __MIRRORER_UPDATES_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

#include "release.h"
#include "strlist.h"

struct update_upstream;

retvalue updates_getpatterns(const char *confdir,struct update_upstream **patterns,int force);

void update_freeupstreams(struct update_upstream *u);

retvalue updates_getupstreams(const struct update_upstream *patterns,struct distribution *distributions);

struct aptmethodrun;
retvalue updates_queuelists(struct aptmethodrun *run,const char *listdir,struct update_upstream *upstreams);
retvalue updates_checklists(const char *listdir,const struct update_upstream *upstreams,int force);

retvalue updates_readlistsfortarget(struct upgradelist *list,struct target *target,const char *listdir,const struct update_upstream *upstreams,int force);

#endif
