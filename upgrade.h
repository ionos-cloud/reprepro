#ifndef __MIRRORER_UPGRADE_H
#define __MIRRORER_UPGRADE_H

#include <db.h>
#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_FILES_H
#include "files.h"
#endif
#ifndef __MIRRORER_PACKAGES_H
#include "packages.h"
#endif

typedef struct s_upgradedb {
	DB *database;
} *upgradedb;

	
/* release the database got by upgrade */
retvalue upgrade_done(upgradedb db);

/* Initialize a database, clear will remove all prior data */
retvalue upgrade_initialize(upgradedb *udb,const char *dbpath,const char *identifier,int clear);

/* Dump the data of the database */
retvalue upgrade_dump(upgradedb db);

/* Initialize a upgrade-cycle for the given distribution, getting
 * the Versions of all packages currently in it...*/
retvalue upgrade_start(upgradedb udb,packagesdb packages);

/* Add all newer packages from the given source to the list */
retvalue upgrade_add(upgradedb udb,const char *filename);

/* Print information about availability and status of packages */
retvalue upgrade_dumpstatus(upgradedb udb,packagesdb packages);

/* Remove all packages, that are no longer available upstream */
retvalue upgrade_deleteoldunavail(upgradedb udb,packagesdb packages);

/* Add all needed files to the list of files to download */
retvalue upgrade_download(upgradedb udb/*, download-db*/);

/* Upgrade everything assuming everything got downloaded already */
retvalue upgrade_do(upgradedb udb,packagesdb packages);


#endif
