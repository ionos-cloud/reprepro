#ifndef __MIRRORER_SOURCES_H
#define __MIRRORER_SOURCES_H

#include <db.h>
#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* get filename and md5andsize from a files: line" */
retvalue sources_getfile(const char *fileline,
		char **basename,
		char **md5andsize);
retvalue sources_getfilekeys(const char *directory,const struct strlist *files,struct strlist *filekeys);

retvalue sources_parse_chunk(const char *chunk,
		char **packagename,
		char **version,
		char **origdirectory,
		struct strlist *files);

/* action taken by sources_add for each sourcepacke missing */
typedef retvalue source_package_action(
	/* the private data passed to sources_add */
	void *data,
	/* the chunk to be added */
	const char *chunk,
	/* the name of the sourcepackage */
	const char *package,
	/* the version */
	const char *version,
	/* the calculated directory it shall be put in (relative to mirrordir) */
	const char *directory,
	/* the directory specified by the chunk. (relative to dists/) */
	const char *olddirectory,
	/* a \n seperated list of md5sums,sizes and filenames, as parseable by sources_getfile */
	const struct strlist *files,
	/* !=NULL if there was a older chunk in the pkgs-database to be replaced */
	const char *oldchunk);

/* call <data> for each package in the "Sources.gz"-style file <source_file> missing in
 * <pkgs> and using <component> as subdir of pool (i.e. "main","contrib",...) for generated paths */
retvalue sources_add(DB *pkgs,const char *component,const char *sources_file,source_package_action action,void *data,int force);

/* remove all references by the given chunk */
retvalue sources_dereference(DB *refs,const char *referee,const char *chunk);
/* add all references by the given data */
retvalue sources_reference(DB *refs,const char *referee,const char *dir,const struct strlist *files);

#endif
