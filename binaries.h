#ifndef __MIRRORER_BINARIES_H
#define __MIRRORER_BINARIES_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_PACKAGES_H
#include "packages.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_TARGET_H
#include "target.h"
#endif

retvalue binaries_calcfilekeys(const char *component,const char *sourcename,const char *basename,struct strlist *filekeys);

/* Functions for the target.h-stuff: */
retvalue binaries_getname(struct target *t,const char *chunk,char **packagename);
retvalue binaries_getversion(struct target *t,const char *chunk,char **version);
retvalue binaries_getinstalldata(struct target *t,const char *packagename,const char *version,const char *chunk,char **control,struct strlist *filekeys,struct strlist *md5sums,struct strlist *origfiles);
retvalue binaries_getfilekeys(struct target *t,const char *chunk,struct strlist *filekeys,struct strlist *md5sums);
char *binaries_getupstreamindex(struct target *target,const char *suite_from,
		const char *component_from,const char *architecture);

#endif
