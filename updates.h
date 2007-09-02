#ifndef REPREPRO_UPDATES_H
#define REPREPRO_UPDATES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_RELEASE_H
#include "release.h"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_FREESPACE_H
#include "freespace.h"
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

retvalue updates_update(struct database *, const char *methoddir, struct update_distribution *distributions, bool nolistsdownload, bool skipold, struct strlist *dereferencedfilekeys, enum spacecheckmode mode, off_t reserveddb, off_t reservedother);
retvalue updates_iteratedupdate(const char *confdir, struct database *, const char *distdir, const char *methoddir, struct update_distribution *distributions, bool nolistsdownload, bool skipold, struct strlist *dereferencedfilekeys, enum exportwhen export, enum spacecheckmode mode, off_t reserveddb, off_t reservedother);
retvalue updates_checkupdate(struct database *, const char *methoddir, struct update_distribution *distributions, bool nolistsdownload, bool skipold);
retvalue updates_predelete(struct database *database, const char *methoddir, struct update_distribution *distributions, bool nolistsdownload, bool skipold, struct strlist *dereferencedfilekeys);

#endif
