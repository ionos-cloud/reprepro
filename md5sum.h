#ifndef __MIRRORER_MD5SUM_H
#define __MIRRORER_MD5SUM_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* result should point to a buffer of at least 33 bytes,
 * bufsize is the size of the buffer to be used, use 0 for
 * standard size. 
 */
retvalue md5sum(char *result,const char *filename,ssize_t bufsize);

/* returns md5sum " " size */
retvalue md5sum_and_size(char **result,const char *filename,ssize_t bufsize);

#endif
