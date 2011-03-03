#ifndef REPREPRO_OVERRIDE_H
#define REPREPRO_OVERRIDE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_CHUNKS_H
#include "chunks.h"
#endif

struct overrideinfo;

/* to avoid typos */
#define PRIORITY_FIELDNAME "Priority"
#define SECTION_FIELDNAME "Section"

void override_free(/*@only@*//*@null@*/struct overrideinfo *info);
retvalue override_read(const char *filename, /*@out@*/struct overrideinfo **info);

/*@null@*//*@dependent@*/const struct overrideinfo *override_search(/*@null@*/const struct overrideinfo *overrides,const char *package);
/*@null@*//*@dependent@*/const char *override_get(/*@null@*/const struct overrideinfo *override,const char *field);


/* add new fields to otherreplaces, but not "Section", or "Priority".
 * incorporates otherreplaces, or frees them on error */
/*@null@*/struct fieldtoadd *override_addreplacefields(const struct overrideinfo *override, /*@only@*/struct fieldtoadd *otherreplaces);

/* as above, but all fields. and may return NULL if there are no overrides */
retvalue override_allreplacefields(const struct overrideinfo *, /*@out@*/struct fieldtoadd **);

#endif
