#ifndef __MIRRORER_PACKAGES_H
#define __MIRRORER_PACKAGES_H

#include <db.h>

/* initialize the packages-database for <identifier> */
DB *packages_initialize(const char *dbpath,const char *identifier);

/* release the packages-database initialized got be packages_initialize */
int packages_done(DB *db);

/* save a given chunk in the database */
int packages_add(DB *packagsdb,const char *package,const char *chunk);
/* replace a save chunk with another */
int packages_replace(DB *packagsdb,const char *package,const char *chunk);
/* remove a given chunk from the database */
int packages_remove(DB *filesdb,const char *package);
/* get the saved chunk from the database */
char *packages_get(DB *packagesdb,const char *package);

/* check for existance of the given version of a package in the arch, 
 * > 0 found
 * = 0 not-found
 * < 0 error
 */
int packages_check(DB *packagesdb,const char *package);

/* print the database to a "Packages" or "Sources" file */
int packages_printout(DB *packagesdb,const char *filename);
int packages_zprintout(DB *packagesdb,const char *filename);

/* action to be called by packages_forall */
typedef int per_package_action(void *data,const char *package,const char *chunk);

/* call action once for each saved chunk: */
int packages_foreach(DB *packagesdb,per_package_action action,void *data);
	
#endif
