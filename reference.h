#ifndef __MIRRORER_REFERENCE_H
#define __MIRRORER_REFERENCE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's happening?"
#endif

DB *references_initialize(const char *dbpath);
retvalue references_done(DB *db);
retvalue references_removedependency(DB* refdb,const char *neededby);
retvalue references_adddependency(DB* refdb,const char *needed,const char *neededby);

/* check if an item is needed, returns RET_NOTHING if not */
retvalue references_isused(DB *refdb,const char *what);

/* print out all referee-referenced-pairs. return 1 if ok, -1 on error */
retvalue references_dump(DB *refdb);

#endif
