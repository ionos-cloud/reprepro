#ifndef __MIRRORER_RELEASE_H
#define __MIRRORER_RELEASE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#endif
#ifndef __MIRRORER_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef __MIRRORER_TARGET_H
#include "target.h"
#endif

/******* Checking contents of release-files ***********/

/* get a strlist with the md5sums of a Release-file */
retvalue release_getchecksums(const char *releasefile,struct strlist *info);

/****** Generate release-files *************/
/* Generate a "Release"-file for an arbitrary  directory */
retvalue release_genrelease(const struct distribution *distribution,const struct target *target,const char *distdir);

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const struct distribution *distribution,const char *distdir,int force);

#endif
