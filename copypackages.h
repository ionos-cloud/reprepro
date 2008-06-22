#ifndef REPREPRO_COPYPACKAGES_H
#define REPREPRO_COPYPACKAGES_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

retvalue copy_by_name(struct database *, struct distribution *into, struct distribution *from, int, const char **, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, struct strlist *dereferenced);
retvalue copy_by_source(struct database *, struct distribution *into, struct distribution *from, int, const char **, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, struct strlist *dereferenced);
retvalue copy_by_formula(struct database *, struct distribution *into, struct distribution *from, const char *formula, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, struct strlist *dereferenced);

retvalue copy_from_file(struct database *, struct distribution *into, const char *component, const char *architecture, const char *packagetype, const char *filename, int, const char **, struct strlist *dereferenced);

retvalue restore_by_name(struct database *, struct distribution *, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, const char *snapshotname, int, const char **, struct strlist *dereferenced);
retvalue restore_by_source(struct database *, struct distribution *, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, const char *snapshotname, int, const char **, struct strlist *dereferenced);
retvalue restore_by_formula(struct database *, struct distribution *, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, const char *snapshotname, const char *filter, struct strlist *dereferenced);

#endif
