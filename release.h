#ifndef __MIRRORER_RELEASE_H
#define __MIRRORER_RELEASE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* check for a <filetocheck> to be have same md5sum and size as <nametocheck> in <releasefile>,
 * returns 1 if ok, == 0 if <nametocheck> not specified, != 1 on error */
retvalue release_checkfile(const char *releasefile,const char *nametocheck,const char *filetocheck);

struct release;

void release_free(struct release *release);
retvalue release_parse(struct release **release,const char *chunk);

/* Generate a "Release"-file for binary directory */
retvalue release_genbinary(const struct release *release,const char *arch,const char *component,const char *distdir);

/* Generate a "Release"-file for source directory */
retvalue release_gensource(const struct release *release,const char *component,const char *distdir);

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const struct release *release,const char *distdir);

#endif
