#ifndef REPREPRO_COPYPACKAGES_H
#define REPREPRO_COPYPACKAGES_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

retvalue copy_by_name(struct database *database, struct distribution *into, struct distribution *from, int argc, const char **argv, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, struct strlist *dereferenced);
retvalue copy_by_source(struct database *database, struct distribution *into, struct distribution *from, int argc, const char **argv, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, struct strlist *dereferenced);


#endif
