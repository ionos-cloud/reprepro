#ifndef __MIRRORER_CHECKIN_H
#define __MIRRORER_CHECKIN_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_DISTRIBUTION_H
#include "distribution.h"
#endif

/* insert the given .changes into the mirror in the <distribution>
 * if forcecomponent, forcesection or forcepriority is NULL
 * get it from the files or try to guess it. */
retvalue changes_add(const char *dbdir,DB *references,filesdb filesdb,const char *forcecomponent,const char *forcearch,const char *forcedsection,const char *forcepriority,struct distribution *distribution,const char *changesfilename,int force);

#endif

