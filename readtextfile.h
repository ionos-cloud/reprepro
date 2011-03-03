#ifndef REPREPRO_READTEXTFILE
#define REPREPRO_READTEXTFILE

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_GLOBALS_H
#include "globals.h"
#warning "What's hapening here?"
#endif

retvalue readtextfilefd(int, const char *, /*@out@*/char **, /*@null@*//*@out@*/size_t *);
retvalue readtextfile(const char *, const char *, /*@out@*/char **, /*@null@*//*@out@*/size_t *);

#endif
