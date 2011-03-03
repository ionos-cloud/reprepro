#ifndef REPREPRO_CHECKINDEB_H
#define REPREPRO_CHECKINDEB_H

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

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if overwrite is not NULL, it will be search for fields to reset for this
 * package. (forcesection and forcepriority have higher priority than the
 * information there),
 * if dereferencedfilekeys is != NULL, add there filekeys that lost a reference */
retvalue deb_add(const char *dbdir,references refs,filesdb filesdb,/*@null@*/const char *forcecomponent,/*@null@*/const char *forcearchitecture,/*@null@*/const char *forcesection,/*@null@*/const char *forcepriority,const char *packagetype,struct distribution *distribution,const char *debfilename,/*@null@*/const char *givenfilekey,/*@null@*/const char *givenmd5sum,/*@null@*/const struct overrideinfo *binoverride,int force,int delete,/*@null@*/struct strlist *dereferencedfilekeys,/*@null@*/trackingdb);

/* in two steps */
struct debpackage;
retvalue deb_addprepared(const struct debpackage *pkg, const char *dbdir,references refs,const char *forcearchitecture,const char *packagetype,struct distribution *distribution,int force,struct strlist *dereferencedfilekeys,struct trackingdata *trackingdata);
retvalue deb_prepare(/*@out@*/struct debpackage **deb,filesdb filesdb,const char * const forcecomponent,const char * const forcearchitecture,const char *forcesection,const char *forcepriority,const char * const packagetype,struct distribution *distribution,const char *debfilename,const char * const givenfilekey,const char * const givenmd5sum,const struct overrideinfo *binoverride,int delete,bool_t needsourceversion);
void deb_free(/*@only@*/struct debpackage *pkg);
#endif
