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
#ifndef __MIRRORER_TARGET_H
#include "target.h"
#endif

/* get filename and md5sum from a files: line" */
retvalue sources_getfile(const char *fileline,
		char **basename,
		char **md5sum);

retvalue sources_parse_getfilekeys(const char *chunk, struct strlist *filekeys);
retvalue sources_parse_getmd5sums(const char *chunk,struct strlist *basenames, struct strlist *md5sums);

/* Look for an old version of the Package in the database.
 * return RET_NOTHING, if there is none at all. */
retvalue sources_lookforold(packagesdb packages,const char *packagename,
					struct strlist *oldfiles);

	
/* call <action> for each package in the "Sources.gz"-style file <source_file> 
 * missing in <pkgs> and using <component> as subdir of pool 
 * (i.e. "main","contrib",...) for generated paths */
retvalue sources_findnew(packagesdb pkgs,const char *component,const char *sources_file,new_package_action action,void *data,int force);

/* Calculate the filelines in a form suitable for chunk_replacefields: */
retvalue sources_calcfilelines(const struct strlist *basenames,const struct strlist *md5sums,char **item);

/* Functions for the target.h-stuff: */
retvalue sources_getname(struct target * t,const char *chunk,char **packagename);
retvalue sources_getversion(struct target *t,const char *chunk,char **version);
retvalue sources_getinstalldata(struct target *t,const char *packagename,const char *version,const char *chunk,char **control,struct strlist *filekeys,struct strlist *md5sums,struct strlist *origfiles);
retvalue sources_getfilekeys(struct target *t,const char *name,const char *chunk,struct strlist *filekeys);
#endif
