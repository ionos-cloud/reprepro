#ifndef __MIRRORER_RELEASE_H
#define __MIRRORER_RELEASE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#endif

/* check for a <filetocheck> to be have same md5sum and size as <nametocheck> in <releasefile>,
 * returns 1 if ok, == 0 if <nametocheck> not specified, != 1 on error */
retvalue release_checkfile(const char *releasefile,const char *nametocheck,const char *filetocheck);

#endif
