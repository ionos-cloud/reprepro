#ifndef __MIRRORER_RELEASE_H
#define __MIRRORER_RELEASE_H

/* check for a <filetocheck> to be have same md5sum and size as <nametocheck> in <releasefile>,
 * returns 1 if ok, == 0 if <nametocheck> not specified, != 1 on error */
int release_checkfile(const char *releasefile,const char *nametocheck,const char *filetocheck);

#endif
