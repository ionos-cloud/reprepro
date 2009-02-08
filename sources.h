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
retvalue sources_calcfilelines(const struct checksumsarray *, /*@out@*/char **item);

/* Functions for the target.h-stuff: */
retvalue sources_getversion(const char *chunk, /*@out@*/char **version);
retvalue sources_getinstalldata(const struct target *t, const char *packagename, const char *version, architecture_t, const char *chunk, char **control, /*@out@*/struct strlist *filekeys, /*@out@*/struct checksumsarray *origfiles);
retvalue sources_getarchitecture(const char *chunk, /*@out@*/architecture_t *);
retvalue sources_getfilekeys(const char *, /*@out@*/struct strlist *);
retvalue sources_getchecksums(const char *, /*@out@*/struct checksumsarray *);
retvalue sources_doreoverride(const struct distribution *,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk);
retvalue sources_retrack(const char *packagename, const char *chunk, trackingdb tracks, struct database *);
retvalue sources_getsourceandversion(const char *chunk, const char *packagename, char **source, char **version);

/* Functions for checkindsc.c and incoming.c: */
struct dsc_headers {
	char *name, *version;
	char *control;
	struct checksumsarray files;
	/* normaly not in a .dsc file: */
	/*@null@*/ char *section, *priority;
};

/* read contents of filename into sources_readdsc.
 * - broken is like signature_readsignedchunk
 * - does not follow retvalue conventions, some fields may be set even when
 *   error returned
 * - no checks for sanity of values, left to the caller */
retvalue sources_readdsc(struct dsc_headers *, const char *filename, const char *filenametoshow, bool *broken);

void sources_done(struct dsc_headers *);

struct overrideinfo;
retvalue sources_complete(const struct dsc_headers *, const char *directory, const struct overrideinfo *override, const char *section, const char *priority, char **newcontrol);

char *calc_source_basename(const char *name, const char *version);
char *calc_sourcedir(component_t, const char *sourcename);
char *calc_filekey(component_t, const char *sourcename, const char *filename);
char *calc_byhanddir(component_t, const char *sourcename, const char *version);
#endif
