#ifndef __MIRRORER_BINARIES_H
#define __MIRRORER_BINARIES_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* get somefields out of a "Packages.gz"-chunk. returns 1 on success, 0 if incomplete, -1 on error */
retvalue binaries_parse_chunk(const char *chunk,char **packagename,char **origfilename,char **sourcename,char **filename,char **md5andsize);

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
	/* the filename (and path relative to dists) found in the chunk */
	const char *origfile,
	/* the calculated filename it should have (without directory) */
	const char *filename,
	/* with directory relative to the pool/-dir */
	const char *filekey,
	/* the expected md5sum and size */
	const char *md5andsize,
	/* the pkgs database had an entry of the same name already
	   (which was considered older), NULL otherwise */
	const char *oldchunk);

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
	void *data);

/* Retrieve the filekey from an older chunk */ 
retvalue binaries_getoldfilekey(const char *oldchunk,const char *ppooldir, char **filekey);

#endif
