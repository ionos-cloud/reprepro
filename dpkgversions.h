#ifndef __MIRRORER_DPKGVERSIONS_H
#define __MIRRORER_DPKGVERSIONS_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning wth?
#endif

/* code taken from dpkg to compare to version string, returns
 * -2 on parsing errors, 1 if newer, 0 if not */
int isVersionNewer(const char *first,const char *second);

/* return RET_OK, if first >> second, RET_NOTHING if not,
 * return error if those are not proper versions */
retvalue dpkgversions_isNewer(const char *first,const char *second);

#endif
