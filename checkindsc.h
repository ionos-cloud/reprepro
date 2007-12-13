#ifndef REPREPRO_CHECKINDSC_H
#define REPREPRO_CHECKINDSC_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif

/* insert the given .dsc into the mirror in <component> in the <distribution>
 * if component is NULL, guess it from the section.
 * if dereferenced_filekeys is != NULL, add there the filekeys that lost a reference*/
retvalue dsc_add(struct database *,/*@null@*/const char *forcecomponent,/*@null@*/const char *forcesection,/*@null@*/const char *forcepriority,struct distribution *distribution,const char *dscfilename,int delete,/*@null@*/struct strlist *dereferencedfilekeys, /*@null@*/trackingdb tracks);

/* in two steps:
 * If basename, filekey and directory are != NULL, then they are used instead
 * of being newly calculated.
 * (And all files are expected to already be in the pool),
 * delete should be D_INPLACE then
 */
struct dscpackage *pkg;
retvalue dsc_prepare(struct dscpackage **dsc, struct database *, const char *forcecomponent, const char *forcesection, const char *forcepriority, struct distribution *distribution, /*@null@*/const char *sourcedir, const char *dscfilename, const char *basename, const char *directory, const char *md5sum, const char *expectedname, const char *expectedversion);
retvalue dsc_addprepared(const struct dscpackage *pkg,struct database *,struct distribution *distribution,/*@null@*/struct strlist *dereferencedfilekeys, /*@null@*/struct trackingdata *trackingdata);
void dsc_free(/*@only@*/struct dscpackage *pkg);


#endif
