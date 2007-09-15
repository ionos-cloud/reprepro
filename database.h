#ifndef REPREPRO_DATABASE_H
#define REPREPRO_DATABASE_H

#ifndef REPREPRO_GLOBALS_H
#include "globals.h"
#endif
#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

struct database;
struct distribution;
struct table;
struct cursor;

retvalue database_create(struct database **, const char *dbdir, struct distribution *, bool fast, bool nopackages, bool allowunused, bool readonly, size_t waitforlock, bool verbosedb);
retvalue database_close(struct database *);

const char *database_directory(struct database *);

retvalue database_openfiles(struct database *, const char *mirrordir);
retvalue database_openreferences(struct database *);
retvalue database_listpackages(struct database *, /*@out@*/struct strlist *);
retvalue database_droppackages(struct database *, const char *);
retvalue database_openpackages(struct database *, const char *identifier, bool readonly, /*@out@*/struct table **);

retvalue table_close(/*@only@*/struct table *);

bool table_isempty(struct table *);

/* retrieve a record from the database, return RET_NOTHING if there is none: */
retvalue table_getrecord(struct table *, const char *, /*@out@*/char **);

retvalue table_adduniqrecord(struct table *, const char *key, const char *data);
retvalue table_replacerecord(struct table *, const char *key, const char *data);
retvalue table_deleterecord(struct table *, const char *key);
retvalue table_checkkey(struct table *, const char *key);

retvalue table_newglobaluniqcursor(struct table *, /*@out@*/struct cursor **);
retvalue table_newduplicatecursor(struct table *, const char *key, /*@out@*/struct cursor **);
bool cursor_nexttemp(struct table *, struct cursor *, /*@out@*/const char **, /*@out@*/const char **);
retvalue cursor_replace(struct table *, struct cursor *, const char *);
retvalue cursor_close(struct table *, /*@only@*/struct cursor *);

#endif
