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

void override_free(struct overrideinfo *info);
retvalue override_read(const char *filename,struct overrideinfo **info);


#endif
