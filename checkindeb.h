#ifndef __MIRRORER_CHECKINDEB_H
#define __MIRRORER_CHECKINDEB_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_DISTRIBUTION_H
#include "distribution.h"
#endif

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <architectures> (and also
 * causing error, if it is not one of them otherwise)
 * ([todo:]using overwrite-database <overwrite>)*/
retvalue deb_add(const char *dbdir,DB *references,DB *filesdb,const char *mirrordir,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *debfilename,const char *givenfilekey,const char *givenmd5sum,int force);
#endif
