#ifndef REPREPRO_RELEASE_H
#define REPREPRO_RELEASE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif

/* Generate a "Release"-file for an arbitrary  directory */
retvalue release_genrelease(const char *distributiondir,const struct distribution *distribution,const struct target *target,const char *filename,bool_t onlyifneeded, struct strlist *releasedfiles);

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const char *distributiondir,const struct distribution *distribution,struct strlist *releasedfiles, int force);

#endif
