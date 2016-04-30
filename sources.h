#ifndef REPREPRO_SOURCES_H
#define REPREPRO_SOURCES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif

/* Functions for the target.h-stuff: */
get_version sources_getversion;
get_installdata sources_getinstalldata;
get_architecture sources_getarchitecture;
get_filekeys sources_getfilekeys;
get_checksums sources_getchecksums;
do_reoverride sources_doreoverride;
do_retrack sources_retrack;
get_sourceandversion sources_getsourceandversion;
complete_checksums sources_complete_checksums;

/* Functions for checkindsc.c and incoming.c: */
struct dsc_headers {
	char *name, *version;
	char *control;
	struct checksumsarray files;
	/* normally not in a .dsc file: */
	/*@null@*/ char *section, *priority;
};

/* read contents of filename into sources_readdsc.
 * - broken is like signature_readsignedchunk
 * - does not follow retvalue conventions, some fields may be set even when
 *   error returned
 * - no checks for sanity of values, left to the caller */
retvalue sources_readdsc(struct dsc_headers *, const char *filename, const char *filenametoshow, bool *broken);

void sources_done(struct dsc_headers *);

struct overridedata;
retvalue sources_complete(const struct dsc_headers *, const char *directory, const struct overridedata *override, const char *section, const char *priority, char **newcontrol);

char *calc_source_basename(const char *name, const char *version);
char *calc_sourcedir(component_t, const char *sourcename);
char *calc_filekey(component_t, const char *sourcename, const char *filename);
char *calc_byhanddir(component_t, const char *sourcename, const char *version);
#endif
