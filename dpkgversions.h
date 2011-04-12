#ifndef REPREPRO_DPKGVERSIONS_H
#define REPREPRO_DPKGVERSIONS_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning wth?
#endif

/* return error if those are not proper versions,
 * otherwise RET_OK and result is <0, ==0 or >0, if first is smaller, equal or larger */
retvalue dpkgversions_cmp(const char *, const char *, /*@out@*/int *);

#endif
