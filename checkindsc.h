#ifndef REPREPRO_CHECKINDSC_H
#define REPREPRO_CHECKINDSC_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef REPREPRO_FILES_H
#include "files.h"
#endif
#ifndef REPREPRO_OVERRIDE_H
#include "override.h"
#endif

/* insert the given .dsc into the mirror in <component> in the <distribution>
 * if component is NULL, guess it from the section.
 * If basename, filekey and directory are != NULL, then they are used instead 
 * of beeing newly calculated. 
 * (And all files are expected to already be in the pool). */
retvalue dsc_add(const char *dbdir,references refs,filesdb filesdb,const char *forcecomponent,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *dscfilename,const char *filekey,const char *basename,const char *directory,const char *md5sum,const struct overrideinfo *srcoverride,int force,int delete);

#endif
