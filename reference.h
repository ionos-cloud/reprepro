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
retvalue references_remove(struct database *, const char *neededby);

/* Add an reference by <identifer> for the given <files>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_insert(struct database *,const char *identifer,
		const struct strlist *files,const struct strlist *exclude);

/* Add an reference by <identifer> for the given <files>,
 * do not error out if reference already exists */
retvalue references_add(struct database *,const char *identifer,const struct strlist *files);

/* Remove reference by <identifer> for the given <oldfiles>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_delete(struct database *, const char *identifer, struct strlist *files, /*@null@*/const struct strlist *exclude);

/* add an reference to a file for an identifier. */
retvalue references_increment(struct database *,const char *needed,const char *needey);

/* delete reference to a file for an identifier */
retvalue references_decrement(struct database *,const char *needed,const char *needey);

/* check if an item is needed, returns RET_NOTHING if not */
retvalue references_isused(struct database *,const char *what);

/* check if a reference is found as expected */
retvalue references_check(struct database *,const char *referee,const struct strlist *what);

#endif
