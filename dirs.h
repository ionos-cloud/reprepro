#ifndef REPREPRO_DIRS_H
#define REPREPRO_DIRS_H

#ifndef REPREPRO_ERROR_H
#warning "What is happening here?"
#include "error.h"
#endif
#ifndef REPREPRO_STRLIST_H
#warning "What is happening here?"
#include "strlist.h"
#endif

/* create recursively all parent directories before the last '/' */
retvalue dirs_make_parent(const char *filename);
/* create dirname and any '/'-separated part of it */
retvalue dirs_make_recursive(const char *directory);

/* Behave like dirname(3) */
retvalue dirs_getdirectory(const char *filename,/*@out@*/char **directory);

const char *dirs_basename(const char *filename);
#endif
