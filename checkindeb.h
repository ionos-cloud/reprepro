#ifndef __MIRRORER_CHECKINDEB_H
#define __MIRRORER_CHECKINDEB_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef __MIRRORER_FILES_H
#include "files.h"
#endif
#ifndef __MIRRORER_OVERRIDE_H
#include "override.h"
#endif

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if overwrite is not NULL, it will be search for fields to reset for this
 * package. (forcesection and forcepriority have higher priority than the
 * information there) */
retvalue deb_add(const char *dbdir,DB *references,filesdb filesdb,const char *forcecomponent,const char *forcearchitecture,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *debfilename,const char *givenfilekey,const char *givenmd5sum,const struct overrideinfo *override,int force);
#endif
