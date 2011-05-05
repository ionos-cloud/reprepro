#ifndef REPREPRO_CHECKINDEB_H
#define REPREPRO_CHECKINDEB_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if overwrite is not NULL, it will be search for fields to reset for this
 * package. (forcesection and forcepriority have higher priority than the
 * information there), */
retvalue deb_add(component_t, const struct atomlist * /*forcearchitectures*/, /*@null@*/const char * /*forcesection*/, /*@null@*/const char * /*forcepriority*/, packagetype_t, struct distribution *, const char * /*debfilename*/, int /*delete*/, /*@null@*/trackingdb);

/* in two steps */
struct debpackage;
retvalue deb_addprepared(const struct debpackage *, const struct atomlist * /*forcearchitectures*/, packagetype_t, struct distribution *, struct trackingdata *);
retvalue deb_prepare(/*@out@*/struct debpackage **, component_t, architecture_t /*forcearchitectures*/, const char * /*forcesection*/, const char * /*forcepriority*/, packagetype_t, struct distribution *, const char * /*debfilename*/, const char * const /*filekey*/, const struct checksums *, const struct strlist * /*allowed_binaries*/, const char * /*expectedsourcename*/, const char * /*expectedsourceversion*/);
void deb_free(/*@only@*/struct debpackage *);
#endif
