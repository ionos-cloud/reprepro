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

typedef struct references *references;


retvalue references_initialize(/*@out@*/references *ref, const char *path);
retvalue references_done(/*@only@*/references ref);

/* remove all references from a given identifier */
retvalue references_remove(references ref,const char *needey);

/* Add an reference by <identifer> for the given <files>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_insert(references ref,const char *identifer,
		const struct strlist *files,const struct strlist *exclude);

/* Remove reference by <identifer> for the given <oldfiles>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_delete(references ref,const char *identifer,
		const struct strlist *files,const struct strlist *exclude);

/* add an reference to a file for an identifier. multiple calls
 * will add multiple references to allow source packages to share
 * files over versions. (as first the new is added, then the old removed) */
retvalue references_increment(references ref,const char *needed,const char *needey);

/* delete *one* reference to a file for an identifier */
retvalue references_decrement(references ref,const char *needed,const char *needey);

/* check if an item is needed, returns RET_NOTHING if not */
retvalue references_isused(references ref,const char *what);

/* check if a reference is found as expected */
retvalue references_check(references ref,const char *referee,const struct strlist *what);

/* print out all referee-referenced-pairs. */
retvalue references_dump(references ref);

#endif
