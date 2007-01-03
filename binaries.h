#ifndef REPREPRO_BINARIES_H
#define REPREPRO_BINARIES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_TARGET_H
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
char *ubinaries_getupstreamindex(struct target *target,const char *suite_from,
		const char *component_from,const char *architecture);
retvalue binaries_doreoverride(const struct distribution *,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk);
retvalue ubinaries_doreoverride(const struct distribution *,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk);
retvalue binaries_retrack(struct target *t,const char *packagename,const char *chunk, trackingdb tracks,references refs);
retvalue binaries_getsourceandversion(struct target *,const char *chunk,const char *packagename,char **source,char **version);

/* Functions for checkindeb.c and incoming.c: */

struct deb_headers {
	char *name,*version;
	char *source,*architecture;
	char *control;
	/* only extracted when requested: */
	/*@null@*/char *sourceversion;
	/* optional fields: */
	/*@null@*/char *section;
	/*@null@*/char *priority;
};

/* read contents of filename into deb_headers.
 * - does not follow retvalue conventions, some fields may be set even when
 *   error returned
 * - no checks for sanity of values, left to the caller */

retvalue binaries_readdeb(struct deb_headers *, const char *filename, bool_t needssourceversion);
void binaries_debdone(struct deb_headers *);

struct overrideinfo;
retvalue binaries_complete(struct deb_headers *,const char *filekey,const char *md5sum,const struct overrideinfo *,const char *section,const char *priority);

#endif
