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

struct overridefile;
struct overridedata;

/* to avoid typos */
#define PRIORITY_FIELDNAME "Priority"
#define SECTION_FIELDNAME "Section"

void override_free(/*@only@*//*@null@*/struct overridefile *);
retvalue override_read(const char *filename, /*@out@*/struct overridefile **, bool /*source*/);

/*@null@*//*@dependent@*/const struct overridedata *override_search(/*@null@*/const struct overridefile *, const char * /*package*/);
/*@null@*//*@dependent@*/const char *override_get(/*@null@*/const struct overridedata *, const char * /*field*/);

/* add new fields to otherreplaces, but not "Section", or "Priority".
 * incorporates otherreplaces, or frees them on error */
/*@null@*/struct fieldtoadd *override_addreplacefields(const struct overridedata *, /*@only@*/struct fieldtoadd *);

/* as above, but all fields. and may return NULL if there are no overrides */
retvalue override_allreplacefields(const struct overridedata *, /*@out@*/struct fieldtoadd **);

#endif
