#ifndef REPREPRO_MD5SUM_H
#define REPREPRO_MD5SUM_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* Calculate the md5sum of the given file,
 * returns RET_NOTHING, if it does not exist.*/
retvalue md5sum_read(const char *filename,/*@out@*/char **result);

/* replace the given name by data of size len and return its md5sum */
retvalue md5sum_replace(const char *filename, const char *data, size_t len, /*@null@*/char **md5sum);
#endif
