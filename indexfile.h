#ifndef REPREPRO_INDEXFILE_H
#define REPREPRO_INDEXFILE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif

struct indexfile;

retvalue indexfile_open(/*@out@*/struct indexfile **, const char *);
retvalue indexfile_close(/*@only@*/struct indexfile *);
bool indexfile_getnext(struct indexfile *, /*@out@*/char **, /*@out@*/char **, /*@out@*/const char **, const struct target *, bool allowwrongarchitecture);

#endif
