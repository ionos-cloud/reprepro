#ifndef __MIRRORER_DIRS_H
#define __MIRRORER_DIRS_H

#ifndef __MIRRORER_ERROR_H
#warning "What is happening here?"
#include "error.h"
#endif

/* create recursively all parent directories before the last '/' */
retvalue dirs_make_parent(const char *filename);
/* create dirname and any '/'-seperated part of it */
retvalue dirs_make_recursive(const char *directory);

#endif
