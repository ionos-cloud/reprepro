#ifndef __MIRRORER_BINARIES_H
#define __MIRRORER_BINARIES_H

/* the type of a action for binaries_add */
typedef int binary_package_action(
	/* the data supplied to binaries_add */
	void *data,
	/* the chunk to be added */
	const char *chunk,
	/* the package name */
	const char *package,
	/* the sourcename */
	const char *sourcename,
	/* the filename (and path relative to dists) found in the chunk */
	const char *oldfile,
	/* the calculated filename it should have (without directory) */
	const char *filename,
	/* with directory relative to the pool/-dir */
	const char *filekey,
	/* the expected md5sum and size */
	const char *md5andsize,
	/* the pkgs database had an entry of the same name already
	   (which was considered older) */
	int hadold);

/* call action for each package in packages_file */
int binaries_add(
	/* the database of already included packages */
	DB *pkgs,
	/* the part (i.e. "main","contrib","non-free") to be used for dirs */
	const char *part,
	/* the file to traverse */
	const char *packages_file,
	/* the action to take for each package to add */
	binary_package_action action,
	/* some data to pass to the action */
	void *data);

#endif
