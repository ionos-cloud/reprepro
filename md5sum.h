#ifndef __MIRRORER_MD5SUM_H
#define __MIRRORER_MD5SUM_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* Calculate the md5sum of the given file, 
 * returns RET_NOTHING, if it does not exist.*/
retvalue md5sum_read(const char *filename,char **result);

/* copy orig to dest and calculate the md5sum while dooing so.
 * return RET_NOTHING, if does not exist, and RET_ERROR_EXIST, if dest
 * is already existing before. */
retvalue md5sum_copy(const char *origfilename,const char *destfilename, 
			char **result);
#endif
