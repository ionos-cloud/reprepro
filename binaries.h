#ifndef __MIRRORER_BINARIES_H
#define __MIRRORER_BINARIES_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* get somefields out of a "Packages.gz"-chunk. returns 1 on success, 0 if incomplete, -1 on error */
retvalue binaries_parse_chunk(const char *chunk,
                    	char **packagename,
                    	char **origfilename,
                    	char **sourcename,
                    	char **basename,
                    	char **md5andsize,
			char **version);

/* Look for an older version of the Package in the database.
 * Set *oldversion, if there is already a newer (or equal) version to
 * <version> and <version> is != NULL */
retvalue binaries_lookforolder(
		DB *packages,const char *packagename,
		const char *newversion,char **oldversion,
		char **oldfilekey);

/* the type of a action for binaries_add */
typedef retvalue binary_package_action(
	/* the data supplied to binaries_add */
	void *data,
	/* the chunk to be added */
	const char *chunk,
	/* the package name */
	const char *package,
	/* the sourcename */
	const char *sourcename,
	/* the filename (including path) as found in the chunk */
	const char *origfile,
	/* the calculated base filename it should have (without directory) */
	const char *basename,
	/* with directory relative to the mirrordir */
	const char *filekey,
	/* the expected md5sum and size */
	const char *md5andsize,
	/* the pkgs database had an entry of the same name already
	   (which was considered older), NULL otherwise */
	const char *oldfilekey);

/* call action for each package in packages_file */
retvalue binaries_add(
	/* the database of already included packages */
	DB *pkgs,
	/* the part (i.e. "main","contrib","non-free") to be used for dirs */
	const char *part,
	/* the file to traverse */
	const char *packages_file,
	/* the action to take for each package to add */
	binary_package_action action,
	/* some data to pass to the action */
	void *data,
	/* == 0 to stop process at first error. */
	int force
	);

#endif
