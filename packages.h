#ifndef __MIRRORER_PACKAGES_H
#define __MIRRORER_PACKAGES_H

#include <db.h>
#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#warning "What's hapening here?"
#endif

typedef struct s_packagesdb {
	char *identifier;
	DB *database;
} *packagesdb;

/* initialize the packages-database for <identifier> */
retvalue packages_initialize(packagesdb *pkgs,const char *dbpath,const char *identifier);

/* The same but calculate the identifier */
retvalue packages_init(packagesdb *pkgs,const char *dbpath,const char *codename,const char *component,const char *architecture);

/* release the packages-database initialized got be packages_initialize */
retvalue packages_done(packagesdb db);

/* save a given chunk in the database */
retvalue packages_add(packagesdb db,const char *package,const char *chunk);
/* replace a save chunk with another */
retvalue packages_replace(packagesdb db,const char *package,const char *chunk);
/* remove a given chunk from the database */
retvalue packages_remove(packagesdb db,const char *package);
/* get the saved chunk from the database,
 * returns RET_NOTHING, if there is none*/
retvalue packages_get(packagesdb db,const char *package,char **chunk);

/* rereference a full database */
typedef retvalue extractfilekeys(const char *,struct strlist *);
retvalue packages_rereference(const char *dbdir,DB *referencesdb,extractfilekeys *extractfilekeys,const char *codename,const char *component,const char *architecture,int force);
retvalue packages_check(const char *dbdir,DB *filesdb,DB *referencesdb,extractfilekeys *extractfilekeys,const char *codename,const char *component,const char *architecture,int force);

/* insert a chunk in the packages database, adding and deleting
 * references and insert files while that. */
retvalue packages_insert(DB *referencesdb, packagesdb packagesdb,
		const char *packagename, const char *controlchunk,
		const struct strlist *files,
		const struct strlist *oldfiles);

/* print the database to a "Packages" or "Sources" file */
// retvalue packages_printout(packagesdb packagesdb,const char *filename);
// retvalue packages_zprintout(packagesdb packagesdb,const char *filename);
/* like packages_printout, but open and close database yourself */
retvalue packages_doprintout(const char *dbpath,const char *dbname,const char *filename);
retvalue packages_dozprintout(const char *dbpath,const char *dbname,const char *filename);

/* action to be called by packages_forall */
typedef retvalue per_package_action(void *data,const char *package,const char *chunk);

/* call action once for each saved chunk: */
retvalue packages_foreach(packagesdb packagesdb,per_package_action action,void *data, int force);

/* The action-type supplied to binary.c and source.c when looking
 * for things to update: */
typedef retvalue new_package_action(
	/* the private data passed to {binaries,sources}_add */
	void *data,
	/* the chunk to be added */
	const char *chunk,
	/* the name of the {binary-,source-}package */
	const char *packagename,
	/* the version */
	const char *version,
	/* the files (relative to mirrordir) it contains */
	const struct strlist *filekeys,
	/* the original files the chunk describes: */
	const struct strlist *origfiles,
	/* the md5sumandsize of theese files requested: */
	const struct strlist *md5sums,
	/* files (r.t. mirrordir) the previous version needed: */
	const struct strlist *oldfilekeys
	);
	
#endif
