#ifndef __MIRRORER_DPKGVERSIONS_H
#define __MIRRORER_DPKGVERSIONS_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning wth?
#endif

/* return error if those are not proper versions,
 * otherwise RET_OK and result is <0, ==0 or >0, if first is smaller, equal or larger */
retvalue dpkgversions_cmp(const char *first,const char *second,int *result);

#endif
