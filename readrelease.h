#ifndef REPREPRO_READRELEASE_H
#define REPREPRO_READRELEASE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_CHECKSUMS_H
#include "checksums.h"
#endif

/* get a strlist with the md5sums of a Release-file */
retvalue release_getchecksums(const char *releasefile, const bool ignore[cs_hashCOUNT], /*@out@*/struct checksumsarray *);

#endif

