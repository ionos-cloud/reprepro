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

struct distribution;
struct table;
struct cursor;

retvalue database_create(struct distribution *, bool fast, bool /*nopackages*/, bool /*allowunused*/, bool /*readonly*/, size_t /*waitforlock*/, bool /*verbosedb*/);
retvalue database_close(void);

retvalue database_openfiles(void);
retvalue database_openreferences(void);
retvalue database_listpackages(/*@out@*/struct strlist *);
retvalue database_droppackages(const char *);
retvalue database_openpackages(const char *, bool /*readonly*/, /*@out@*/struct table **);
retvalue database_openreleasecache(const char *, /*@out@*/struct table **);
retvalue database_opentracking(const char *, bool /*readonly*/, /*@out@*/struct table **);
retvalue database_translate_filelists(void);
retvalue database_translate_legacy_checksums(bool /*verbosedb*/);
bool database_allcreated(void);

retvalue table_close(/*@only@*/struct table *);

retvalue database_haspackages(const char *);

bool table_recordexists(struct table *, const char *);
/* retrieve a record from the database, return RET_NOTHING if there is none: */
retvalue table_getrecord(struct table *, const char *, /*@out@*/char **, /*@out@*/ /*@null@*/ size_t *);
retvalue table_gettemprecord(struct table *, const char *, /*@out@*//*@null@*/const char **, /*@out@*//*@null@*/size_t *);
retvalue table_getpair(struct table *, const char *, const char *, /*@out@*/const char **, /*@out@*/size_t *);

retvalue table_adduniqsizedrecord(struct table *, const char * /*key*/, const char * /*data*/, size_t /*data_size*/, bool /*allowoverwrote*/, bool /*nooverwrite*/);
retvalue table_adduniqrecord(struct table *, const char * /*key*/, const char * /*data*/);
retvalue table_addrecord(struct table *, const char * /*key*/, const char * /*data*/, size_t /*len*/, bool /*ignoredups*/);
retvalue table_replacerecord(struct table *, const char *key, const char *data);
retvalue table_deleterecord(struct table *, const char *key, bool ignoremissing);
retvalue table_checkrecord(struct table *, const char *key, const char *data);
retvalue table_removerecord(struct table *, const char *key, const char *data);

retvalue table_newglobalcursor(struct table *, bool /*duplicate*/, /*@out@*/struct cursor **);
retvalue table_newduplicatecursor(struct table *, const char *, long long, /*@out@*/struct cursor **, /*@out@*/const char **, /*@out@*/const char **, /*@out@*/size_t *);
retvalue table_newduplicatepairedcursor(struct table *, const char *, /*@out@*/struct cursor **, /*@out@*/const char **, /*@out@*/const char **, /*@out@*/size_t *);
retvalue table_newpairedcursor(struct table *, const char *, const char *, /*@out@*/struct cursor **, /*@out@*//*@null@*/const char **, /*@out@*//*@null@*/size_t *);
bool cursor_nexttempdata(struct table *, struct cursor *, /*@out@*/const char **, /*@out@*/const char **, /*@out@*/size_t *);
bool cursor_nextpair(struct table *, struct cursor *, /*@null@*//*@out@*/const char **, /*@out@*/const char **, /*@out@*/const char **, /*@out@*/size_t *);
retvalue cursor_replace(struct table *, struct cursor *, const char *, size_t);
retvalue cursor_delete(struct table *, struct cursor *, const char *, /*@null@*/const char *);
retvalue cursor_close(struct table *, /*@only@*/struct cursor *);

#endif
