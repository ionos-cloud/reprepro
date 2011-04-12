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

/* create a directory, return RET_NOTHING if already existing */
retvalue dirs_create(const char *);
/* create recursively all parent directories before the last '/' */
retvalue dirs_make_parent(const char *);
/* create dirname and any '/'-separated part of it */
retvalue dirs_make_recursive(const char *);
/* create directory and parents as needed, and save count to remove them later */
retvalue dir_create_needed(const char *, int *);
void dir_remove_new(const char *, int);

/* Behave like dirname(3) */
retvalue dirs_getdirectory(const char *, /*@out@*/char **);

const char *dirs_basename(const char *);

bool isdir(const char *);
#endif
