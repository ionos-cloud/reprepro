#ifndef REPREPRO_MD5SUM_H
#define REPREPRO_MD5SUM_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* Calculate the md5sum of the given file, 
 * returns RET_NOTHING, if it does not exist.*/
retvalue md5sum_read(const char *filename,/*@out@*/char **result);

/* copy orig to dest and calculate the md5sum while dooing so.
 * return RET_NOTHING, if does not exist, and RET_ERROR_EXIST, if dest
 * is already existing before. */
retvalue md5sum_copy(const char *origfilename,const char *destfilename, 
			/*@out@*/char **result);
/* same as above, but delete existing files and try to hardlink first. */
retvalue md5sum_place(const char *origfilename,const char *destfilename, 
			/*@out@*/char **result);
/* return RET_OK, if fullfilename is there and has md5sum md5sum,
 * return RET_NOTHING, if it is not there,
 * return RET_NOTHING and delete it (and warn if warnifwrong) if it has wrong */
retvalue md5sum_ensure(const char *fullfilename,const char *md5sum,bool_t warnifwrong);

struct MD5Context;
retvalue md5sum_genstring(char **md5,struct MD5Context *context,off_t filesize);
#endif
