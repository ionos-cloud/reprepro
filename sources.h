#ifndef __MIRRORER_SOURCES_H
#define __MIRRORER_SOURCES_H

#include <db.h>
#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* traverse through a '\n' sepeated lit of "<md5sum> <size> <filename>" 
 * > 0 while entires found, ==0 when not, <0 on error */
retvalue sources_getfile(const char **files,char **filename,char **md5andsize);

/* action taken by sources_add for each sourcepacke missing */
typedef retvalue source_package_action(
	/* the private data passed to sources_add */
	void *data,
	/* the chunk to be added */
	const char *chunk,
	/* the name of the sourcepackage */
	const char *package,
	/* the calculated directory it shall be put in (relative to pool/) */
	const char *directory,
	/* the directory specified by the chunk. (relative to dists/) */
	const char *olddirectory,
	/* a \n seperated list of md5sums,sizes and filenames, as parseable by sources_getfile */
	const char *files,
	/* !=0 if there was a older chunk in the pkgs-database to be replaced */
	int hadold);

/* call <data> for each package in the "Sources.gz"-style file <source_file> missing in
 * <pkgs> and using <part> as subdir of pool (i.e. "main","contrib",...) for generated paths */
retvalue sources_add(DB *pkgs,const char *part,const char *sources_file,source_package_action action,void *data);

#endif
