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
/* check in fileinfo for <nametocheck> to have md5sum and size <expected> *
 * returns RET_OK if ok, == RET_NOTHING if not found, error otherwise     */
//retvalue release_searchchecksum(const struct strlist *fileinfos, const char *nametocheck, const char *expected);

/* check in fileinfo for <nametocheck> to have md5sum and size of <filename> *
 * returns RET_OK if ok, error otherwise     */
retvalue release_check(const struct strlist *fileinfos, const char *nametocheck, const char *filename);
	
/* check for a <filetocheck> to be have same md5sum and size as <nametocheck> in <releasefile>,
 * returns 1 if ok, == 0 if <nametocheck> not specified, != 1 on error */
retvalue release_checkfile(const char *releasefile,const char *nametocheck,const char *filetocheck);

/****** Generate release-files *************/
/* Generate a "Release"-file for an arbitrary  directory */
retvalue release_genrelease(const struct distribution *distribution,const target target,const char *distdir);

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const struct distribution *distribution,const char *distdir,const char *chunk,int force);

#endif
