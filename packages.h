#ifndef __MIRRORER_PACKAGES_H
#define __MIRRORER_PACKAGES_H

#include <db.h>

DB *initialize_packages(const char *dbpath,const char *identifier);

int addpackage(DB *packagsdb,const char *package,const char *chunk);
int replacepackage(DB *packagsdb,const char *package,const char *chunk);
int removepackage(DB *filesdb,const char *package);

char *getpackage(DB *packagesdb,const char *package);

/* check for existance of the given version of a package in the arch, 
 * > 0 found
 * = 0 not-found
 * < 0 error
 */
int checkpackage(DB *packagesdb,const char *package);

int packages_printout(DB *packagesdb,const char *filename);
int packages_zprintout(DB *packagesdb,const char *filename);

#endif
