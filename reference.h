#ifndef REPREPRO_REFERENCE_H
#define REPREPRO_REFERENCE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's happening?"
#endif

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#warning "What's happening?"
#endif

#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

struct references;

/* remove all references from a given identifier */
retvalue references_remove(const char *neededby);

/* Add an reference by <identifier> for the given <files>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_insert(const char *, const struct strlist *, const struct strlist * /*exclude*/);

/* Add an reference by <identifier> for the given <files>,
 * do not error out if reference already exists */
retvalue references_add(const char *, const struct strlist *);

/* Remove reference by <identifier> for the given <oldfiles>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_delete(const char *, const struct strlist *, /*@null@*/const struct strlist * /*exclude*/);

/* add an reference to a file for an identifier. */
retvalue references_increment(const char * /*needed*/, const char * /*needey*/);

/* delete reference to a file for an identifier */
retvalue references_decrement(const char * /*needed*/, const char * /*needey*/);

/* check if an item is needed, returns RET_NOTHING if not */
retvalue references_isused(const char *);

/* check if a reference is found as expected */
retvalue references_check(const char * /*referee*/, const struct strlist */*what*/);

/* output all references to stdout */
retvalue references_dump(void);

#endif
