#ifndef REPREPRO_DATABASE_P_H
#define REPREPRO_DATABASE_P_H

#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

struct references;
struct filesdb;

struct database {
	char *directory;
	/* for the files database: */
	char *mirrordir;
	struct table *files, *contents;
	/* for the references database: */
	struct table *references;
	/* internal stuff: */
	bool locked, verbose;
	int dircreationdepth;
	bool nopackages, readonly,
	     packagesdatabaseopen, trackingdatabaseopen;
	char *version, *lastsupportedversion,
	     *dbversion, *lastsupporteddbversion;
	struct {
		bool createnewtables;
	} capabilities ;
};

retvalue database_listsubtables(struct database *,const char *,/*@out@*/struct strlist *);
retvalue database_dropsubtable(struct database *, const char *table, const char *subtable);

#endif
