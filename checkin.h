#ifndef REPREPRO_CHECKIN_H
#define REPREPRO_CHECKIN_H

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
#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif

/* insert the given .changes into the mirror in the <distribution>
 * if forcecomponent, forcesection or forcepriority is NULL
 * get it from the files or try to guess it.
 * if dereferencedfilekeys is != NULL, add filekeys that lost reference,
 * if tracks != NULL, update/add tracking information there... */
retvalue changes_add(/*@null@*/trackingdb, const struct atomlist * /*packagetypes*/, component_t, const struct atomlist * /*forcearchitecture*/, /*@null@*/const char * /*forcesection*/, /*@null@*/const char * /*forcepriority*/, struct distribution *, const char * /*changesfilename*/, int /*delete*/);

#endif

