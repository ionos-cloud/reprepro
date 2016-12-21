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
struct package;

retvalue indexfile_open(/*@out@*/struct indexfile **, const char *, enum compression);
retvalue indexfile_close(/*@only@*/struct indexfile *);
bool indexfile_getnext(struct indexfile *, /*@out@*/struct package *, struct target *, bool allowwrongarchitecture);

#endif
