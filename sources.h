#ifndef REPREPRO_SOURCES_H
#define REPREPRO_SOURCES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif

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

/* Functions for checkindsc.c and incoming.c: */
struct dsc_headers {
	char *name, *version;
	char *control;
	struct strlist basenames, md5sums;
	/* normaly not in a .dsc file: */
	/*@null@*/ char *section, *priority;
};

/* read contents of filename into sources_readdsc.
 * - broken is like signature_readsignedchunk
 * - does not follow retvalue conventions, some fields may be set even when
 *   error returned
 * - no checks for sanity of values, left to the caller */
retvalue sources_readdsc(struct dsc_headers *, const char *filename, bool_t *broken);

void sources_done(struct dsc_headers *);

struct overrideinfo;
retvalue sources_complete(struct dsc_headers *, const char *directory, const struct overrideinfo *override, const char *section, const char *priority);

#endif
