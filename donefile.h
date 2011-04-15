#ifndef REPREPRO_DONEFILE_H
#define REPREPRO_DONEFILE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif

struct checksums;

struct markdonefile;
retvalue markdone_create(const char *, /*@out@*/struct markdonefile **);
void markdone_finish(/*@only@*/struct markdonefile *);
void markdone_target(struct markdonefile *, const char *);
void markdone_index(struct markdonefile *, const char *, const struct checksums *);
void markdone_cleaner(struct markdonefile *);

struct donefile;
retvalue donefile_open(const char *, /*@out@*/struct donefile **);
void donefile_close(/*@only@*/struct donefile *);
retvalue donefile_nexttarget(struct donefile *, /*@out@*/const char **);
bool donefile_nextindex(struct donefile *, /*@out@*/const char **, /*@out@*/struct checksums **);
bool donefile_iscleaner(struct donefile *);

#endif
