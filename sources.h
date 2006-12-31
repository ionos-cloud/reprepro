#ifndef REPREPRO_SOURCES_H
#define REPREPRO_SOURCES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif

retvalue sources_parse_getmd5sums(const char *chunk,/*@out@*/struct strlist *basenames, /*@out@*/struct strlist *md5sums);

/* Calculate the filelines in a form suitable for chunk_replacefields: */
retvalue sources_calcfilelines(const struct strlist *basenames,const struct strlist *md5sums,/*@out@*/char **item);

/* Functions for the target.h-stuff: */
retvalue sources_getname(struct target * t,const char *chunk,/*@out@*/char **packagename);
retvalue sources_getversion(struct target *t,const char *chunk,/*@out@*/char **version);
retvalue sources_getinstalldata(struct target *t,const char *packagename,const char *version,const char *chunk,char **control,/*@out@*/struct strlist *filekeys,/*@out@*/struct strlist *md5sums,/*@out@*/struct strlist *origfiles);
retvalue sources_getfilekeys(struct target *t,const char *chunk,/*@out@*/struct strlist *filekeys,/*@out@*/struct strlist *md5sums);
char *sources_getupstreamindex(struct target *target,const char *suite_from,
		const char *component_from,const char *architecture);
retvalue sources_doreoverride(const struct distribution *,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk);
retvalue sources_retrack(struct target *t,const char *packagename,const char *chunk, trackingdb tracks,references refs);
retvalue sources_getsourceandversion(struct target *,const char *chunk,const char *packagename,char **source,char **version);
#endif
