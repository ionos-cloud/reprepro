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

struct overrideinfo {
	struct overrideinfo *next;
	char *packagename;
	struct strlist fields;
};

/* to avoid typos */
#define PRIORITY_FIELDNAME "Priority"
#define SECTION_FIELDNAME "Section"

void override_free(/*@only@*//*@null@*/struct overrideinfo *info);
/* when filename does not start with '/' read override info from overridedir/filename,
 * otherwise from filename directly.. */
retvalue override_read(const char *overridedir,const char *filename,/*@out@*/struct overrideinfo **info);

const struct overrideinfo *override_search(const struct overrideinfo *overrides,const char *package);
const char *override_get(const struct overrideinfo *override,const char *field);


/* add new fields to otherreplaces, but not "Section", or "Priority".
 * incorporates otherreplaces, or frees them on error */
struct fieldtoadd *override_addreplacefields(const struct overrideinfo *override,
		struct fieldtoadd *otherreplaces);

#endif
