#ifndef REPREPRO_MPRINTF
#define REPREPRO_MPRINTF

#include <stdarg.h>

/* This is just a asprintf-wrapper to be more easily used
 * and alwasy returns NULL on error */

/*@null@*/char * mprintf(const char *fmt,...) __attribute__ ((format (printf, 1, 2))); 
/*@null@*/char * vmprintf(const char *fmt,va_list va);

#endif
