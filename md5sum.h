#ifndef __MD5SUM
#define __MD5SUM

/* result should point to a buffer of at least 33 bytes,
 * bufsize is the size of the buffer to be used, use 0 for
 * standard size. 
 */
int md5sum(char *result,const char *filename,ssize_t bufsize);

/* returns md5sum " " size */
int md5sum_and_size(char **result,const char *filename,ssize_t bufsize);

/* return of 0 means no error */

#endif
