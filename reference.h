#ifndef __MIRRORER_REFERENCE_H
#define __MIRRORER_REFERENCE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's happening?"
#endif

DB *references_initialize(const char *dbpath);
retvalue references_done(DB *db);

/* remove all references from a given identifier */
retvalue references_remove(DB* refdb,const char *neededby);

/* add an reference to a file for an identifier. multiple calls
 * will add multiple references to allow source packages to share
 * files over versions. (as first the new is added, then the old removed) */
retvalue references_increment(DB* refdb,const char *needed,const char *neededby);

/* delete *one* reference to a file for an identifier */
retvalue references_decrement(DB* refdb,const char *needed,const char *neededby);

/* check if an item is needed, returns RET_NOTHING if not */
retvalue references_isused(DB *refdb,const char *what);

/* check if a reference is found as expected */
retvalue references_check(DB *refdb,const char *what,const char *by);

/* print out all referee-referenced-pairs. */
retvalue references_dump(DB *refdb);

#endif
