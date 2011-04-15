#ifndef REPREPRO_DATABASE_P_H
#define REPREPRO_DATABASE_P_H

#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

extern /*@null@*/ struct table *rdb_checksums, *rdb_contents;
extern /*@null@*/ struct table *rdb_references;

retvalue database_listsubtables(const char *, /*@out@*/struct strlist *);
retvalue database_dropsubtable(const char *, const char *);

#endif
