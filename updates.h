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

retvalue updates_getupstreams(const struct update_upstream *patterns,const struct distribution *distributions,struct update_upstream **upstreams);

retvalue updates_queuelists(struct aptmethodrun *run,const char *listdir,struct update_upstream *upstreams);
retvalue updates_checklists(const char *listdir,const struct update_upstream *upstreams,int force);
#endif
