#ifndef REPREPRO_BINARIES_H
#define REPREPRO_BINARIES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif
#ifndef REPREPRO_CHECKSUMS_H
#include "checksums.h"
#endif


/* Functions for the target.h-stuff: */
retvalue binaries_getname(struct target *t,const char *chunk,char **packagename);
retvalue binaries_getversion(struct target *t,const char *chunk,char **version);
retvalue binaries_getinstalldata(struct target *t, const char *packagename, const char *version, const char *chunk, /*@out@*/char **control, /*@out@*/struct strlist *filekeys, /*@out@*/struct checksumsarray *origfiles);
retvalue binaries_getfilekeys(const char *chunk, /*@out@*/struct strlist *);
retvalue binaries_getchecksums(const char *chunk, /*@out@*/struct checksumsarray *);
char *binaries_getupstreamindex(struct target *target,const char *suite_from,
		const char *component_from,const char *architecture);
char *ubinaries_getupstreamindex(struct target *target,const char *suite_from,
		const char *component_from,const char *architecture);
retvalue binaries_doreoverride(const struct distribution *,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk);
retvalue ubinaries_doreoverride(const struct distribution *,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk);
retvalue binaries_retrack(struct target *t,const char *packagename,const char *chunk, trackingdb tracks,struct database *);
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

retvalue binaries_readdeb(struct deb_headers *, const char *filename, bool needssourceversion);
void binaries_debdone(struct deb_headers *);

retvalue binaries_calcfilekeys(const char *component,const struct deb_headers *,const char *packagetype,struct strlist *filekeys);

struct overrideinfo;
retvalue binaries_complete(const struct deb_headers *, const char *filekey, const struct checksums *, const struct overrideinfo *, const char *section, const char *priority, char **newcontrol);

retvalue binaries_adddeb(const struct deb_headers *,struct database *,const char *forcearchitecture,const char *packagetype,struct distribution *,struct strlist *dereferencedfilekeys,struct trackingdata *,const char *component,const struct strlist *filekeys,const char *control);
retvalue binaries_checkadddeb(const struct deb_headers *, struct database *, const char *forcearchitecture, const char *packagetype, struct distribution *, bool tracking, const char *component, bool permitnewerold);
#endif
