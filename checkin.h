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
 * if dereferencedfilekeys is != NULL, add filekeys that lost reference */
retvalue changes_add(const char *dbdir,references refs,filesdb filesdb,const char *packagetypeonly,const char *forcecomponent,const char *forcearchitecture,const char *forcesection,const char *forcepriority,struct distribution *distribution,const struct overrideinfo *srcoverride,const struct overrideinfo *binoverride,const char *changesfilename,int force,int delete,struct strlist *dereferencedfilkekeys);

#endif

