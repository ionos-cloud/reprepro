#ifndef __MIRRORER_OVERRIDE_H
#define __MIRRORER_OVERRIDE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#endif
#ifndef __MIRRORER_CHUNKS_H
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

void override_free(struct overrideinfo *info);
retvalue override_read(const char *filename,struct overrideinfo **info);

const struct overrideinfo *override_search(const struct overrideinfo *overrides,const char *package);
const char *override_get(const struct overrideinfo *override,const char *field);


/* add new fields to otherreplaces, but not "Section", or "Priority".
 * incorporates otherreplaces, or frees them on error */
struct fieldtoadd *override_addreplacefields(const struct overrideinfo *override,
		struct fieldtoadd *otherreplaces);

#endif
