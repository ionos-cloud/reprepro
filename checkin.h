#ifndef REPREPRO_CHECKIN_H
#define REPREPRO_CHECKIN_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef REPREPRO_OVERRIDE_H
#include "override.h"
#endif

/* insert the given .changes into the mirror in the <distribution>
 * if forcecomponent, forcesection or forcepriority is NULL
 * get it from the files or try to guess it.
 * if dereferencedfilekeys is != NULL, add filekeys that lost reference,
 * if tracks != NULL, update/add tracking information there... */
retvalue changes_add(const char *dbdir,/*@null@*/trackingdb tracks,references refs,filesdb filesdb,/*@null@*/const char *packagetypeonly,/*@null@*/const char *forcecomponent,/*@null@*/const char *forcearchitecture,/*@null@*/const char *forcesection,/*@null@*/const char *forcepriority,struct distribution *distribution,const struct alloverrides *ao,const char *changesfilename,int delete,/*@null@*/struct strlist *dereferencedfilekeys);

#endif

