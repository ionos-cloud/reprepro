#ifndef __MIRRORER_MD5SUM_H
#define __MIRRORER_MD5SUM_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* returns md5sum " " size */
retvalue md5sum_and_size(char **result,const char *filename,ssize_t bufsize);

#endif
