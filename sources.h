#ifndef __MIRRORER_SOURCES_H
#define __MIRRORER_SOURCES_H

#include <db.h>
#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_PACKAGES_H
#include "packages.h"
#warning "What's hapening here?"
#endif

/* get filename and md5andsize from a files: line" */
retvalue sources_getfile(const char *fileline,
		char **basename,
		char **md5andsize);

retvalue sources_parse_getfilekeys(const char *chunk, struct strlist *filekeys);
retvalue sources_parse_getmd5sums(const char *chunk,struct strlist *basenames, struct strlist *md5andsizes);

/* Look for an old version of the Package in the database.
 * return RET_NOTHING, if there is none at all. */
retvalue sources_lookforold(DB *packages,const char *packagename,
					struct strlist *oldfiles);

	
/* call <action> for each package in the "Sources.gz"-style file <source_file> 
 * missing in <pkgs> and using <component> as subdir of pool 
 * (i.e. "main","contrib",...) for generated paths */
retvalue sources_findnew(DB *pkgs,const char *component,const char *sources_file,new_package_action action,void *data,int force);

/* Add a source package to a distribution, removing previous versions
 * of it, if necesary. */
retvalue sources_addtodist(const char *dbpath,DB *references,const char *codename,const char *component,const char *package,const char *version,const char *controlchunk,const struct strlist *filekeys);

/* Calculate the filelines in a form suitable for chunk_replacefields: */
retvalue sources_calcfilelines(const struct strlist *basenames,const struct strlist *md5sums,char **item);
#endif
