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

retvalue updates_getpatterns(/*@out@*/struct update_pattern **);

void updates_freepatterns(/*@only@*/struct update_pattern *p);
void updates_freeupdatedistributions(/*@only@*/struct update_distribution *d);

retvalue updates_calcindices(struct update_pattern *, struct distribution *, bool fast, /*@out@*/struct update_distribution **);

retvalue updates_update(struct database *, struct update_distribution *, bool nolistsdownload, bool skipold, enum spacecheckmode mode, off_t reserveddb, off_t reservedother);
retvalue updates_checkupdate(struct database *, struct update_distribution *, bool nolistsdownload, bool skipold);
retvalue updates_dumpupdate(struct database *, struct update_distribution *, bool nolistsdownload, bool skipold);
retvalue updates_predelete(struct database *, struct update_distribution *, bool nolistsdownload, bool skipold);

#endif
