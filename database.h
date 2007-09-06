#ifndef REPREPRO_DATABASE_H
#define REPREPRO_DATABASE_H

#ifndef REPREPRO_GLOBALS_H
#include "globals.h"
#endif
#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif

struct database;
struct distribution;

retvalue database_create(struct database **, const char *dbdir, struct distribution *alldistributions, bool fast, bool readonly, size_t waitforlock);
retvalue database_close(struct database *);

const char *database_directory(struct database *);

retvalue database_openfiles(struct database *, const char *mirrordir);
retvalue database_openreferences(struct database *);

#endif
