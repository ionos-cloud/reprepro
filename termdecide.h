#ifndef REPREPRO_TERMDECIDE_H
#define REPREPRO_TERMDECIDE_H

#ifndef REPREPRO_TERMS_H
#include "terms.h"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif

/* decide based on a chunk, (warning: string comparisons even for version!)*/
retvalue term_decidechunk(const term *, const char *, /*@null@*/const void *);

retvalue term_compilefortargetdecision(/*@out@*/term **, const char *);
retvalue term_decidechunktarget(const term *, const char *, const struct target *);



#endif
