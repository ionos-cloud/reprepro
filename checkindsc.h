#ifndef REPREPRO_CHECKINDSC_H
#define REPREPRO_CHECKINDSC_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef REPREPRO_FILES_H
#include "files.h"
#endif
#ifndef REPREPRO_OVERRIDE_H
#include "override.h"
#endif

/* insert the given .dsc into the mirror in <component> in the <distribution>
 * if component is NULL, guess it from the section.
 * if dereferenced_filekeys is != NULL, add there the filekeys that lost a reference*/
retvalue dsc_add(const char *dbdir,references refs,filesdb filesdb,/*@null@*/const char *forcecomponent,/*@null@*/const char *forcesection,/*@null@*/const char *forcepriority,struct distribution *distribution,const char *dscfilename,/*@null@*/const struct overrideinfo *srcoverride,int delete,/*@null@*/struct strlist *dereferencedfilekeys, bool_t onlysigned, /*@null@*/trackingdb tracks);

/* in two steps:
 * If basename, filekey and directory are != NULL, then they are used instead 
 * of being newly calculated. 
 * (And all files are expected to already be in the pool),
 * delete should be D_INPLACE then
 */
struct dscpackage *pkg;
retvalue dsc_prepare(struct dscpackage **dsc,filesdb filesdb,/*@null@*/const char *forcecomponent,/*@null@*/const char *forcesection,/*@null@*/const char *forcepriority,struct distribution *distribution,/*@null@*/const char *sourcedir, const char *dscfilename,/*@null@*/const char *filekey,/*@null@*/const char *basename,/*@null@*/const char *directory,/*@null@*/const char *md5sum,/*@null@*/const struct overrideinfo *srcoverride,int delete, bool_t onlysigned, const char *expectedname, const char *expectedversion);
retvalue dsc_addprepared(const struct dscpackage *pkg,const char *dbdir,references refs,struct distribution *distribution,/*@null@*/struct strlist *dereferencedfilekeys, /*@null@*/struct trackingdata *trackingdata);
void dsc_free(/*@only@*/struct dscpackage *pkg);


#endif
