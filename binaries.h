#ifndef __MIRRORER_BINARIES_H
#define __MIRRORER_BINARIES_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_PACKAGES_H
#include "packages.h"
#warning "What's hapening here?"
#endif

/* get files out of a "Packages.gz"-chunk. */
retvalue binaries_parse_getfiles(const char *chunk,struct strlist *files);

/* Look for an older version of the Package in the database.
 * return RET_NOTHING if there is none, otherwise
 * Set *oldversion, if there is already a newer (or equal) version to
 * <version>  */
retvalue binaries_lookforolder(
		DB *packages,const char *packagename,
		const char *newversion,char **oldversion,
		struct strlist *oldfilekeys);

/* call action for each package in packages_file, not already in pkgs. */
retvalue binaries_findnew(
	/* the database of already included packages */
	DB *pkgs,
	/* the part (i.e. "main","contrib","non-free") to be used for dirs */
	const char *part,
	/* the file to traverse */
	const char *packages_file,
	/* the action to take for each package to add */
	new_package_action action,
	/* some data to pass to the action */
	void *data,
	/* == 0 to stop process at first error. */
	int force
	);

#endif
