#ifndef REPREPRO_SOURCES_H
#define REPREPRO_SOURCES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif

retvalue sources_parse_getmd5sums(const char *chunk,struct strlist *basenames, struct strlist *md5sums);

/* Calculate the filelines in a form suitable for chunk_replacefields: */
retvalue sources_calcfilelines(const struct strlist *basenames,const struct strlist *md5sums,char **item);

/* Functions for the target.h-stuff: */
retvalue sources_getname(struct target * t,const char *chunk,char **packagename);
retvalue sources_getversion(struct target *t,const char *chunk,char **version);
retvalue sources_getinstalldata(struct target *t,const char *packagename,const char *version,const char *chunk,char **control,struct strlist *filekeys,struct strlist *md5sums,struct strlist *origfiles);
retvalue sources_getfilekeys(struct target *t,const char *chunk,struct strlist *filekeys,struct strlist *md5sums);
char *sources_getupstreamindex(struct target *target,const char *suite_from,
		const char *component_from,const char *architecture);
#endif
