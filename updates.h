#ifndef REPREPRO_UPDATES_H
#define REPREPRO_UPDATES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_REFERENCES_H
#include "reference.h"
#endif
#ifndef REPREPRO_RELEASE_H
#include "release.h"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

struct update_pattern;
struct update_origin;
struct update_target;
struct update_distribution;

retvalue updates_getpatterns(const char *confdir,/*@out@*/struct update_pattern **patterns);

void updates_freepatterns(/*@only@*/struct update_pattern *p);
void updates_freeupdatedistributions(/*@only@*/struct update_distribution *d);

retvalue updates_calcindices(const char *listdir,const struct update_pattern *patterns,struct distribution *distributions,/*@out@*/struct update_distribution **update_distributions);

/* remove all files ${listdir}/${distribution}_* that will not be needed. */
retvalue updates_clearlists(const char *listdir,struct update_distribution *distributions);

retvalue updates_update(const char *dbdir,const char *methoddir,filesdb filesdb,references refs,struct update_distribution *distributions,int force,bool_t nolistsdownload,bool_t skipold,struct strlist *dereferencedfilekeys);
retvalue updates_iteratedupdate(const char *confdir,const char *dbdir,const char *distdir,const char *methoddir,filesdb filesdb,references refs,struct update_distribution *distributions,int force,bool_t nolistsdownload,bool_t skipold,struct strlist *dereferencedfilekeys);
retvalue updates_checkupdate(const char *dbdir,const char *methoddir,struct update_distribution *distributions,int force,bool_t nolistsdownload,bool_t skipold);

#endif
