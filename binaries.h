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
get_version binaries_getversion;
get_installdata binaries_getinstalldata;
get_architecture binaries_getarchitecture;
get_filekeys binaries_getfilekeys;
get_checksums binaries_getchecksums;
do_reoverride binaries_doreoverride;
do_reoverride ubinaries_doreoverride;
do_retrack binaries_retrack;
get_sourceandversion binaries_getsourceandversion;
complete_checksums binaries_complete_checksums;

/* Functions for checkindeb.c and incoming.c: */

struct deb_headers {
	char *name, *version;
	char *source;
	architecture_t architecture;
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

retvalue binaries_readdeb(struct deb_headers *, const char *filename);
void binaries_debdone(struct deb_headers *);

retvalue binaries_calcfilekeys(component_t, const struct deb_headers *, packagetype_t, /*@out@*/struct strlist *);

struct overridedata;
retvalue binaries_complete(const struct deb_headers *, const char * /*filekey*/, const struct checksums *, const struct overridedata *, const char * /*section*/, const char * /*priority*/, char **/*newcontrol_p*/);

retvalue binaries_adddeb(const struct deb_headers *, const struct atomlist */*forcedarchitectures*/, packagetype_t, struct distribution *, /*@null@*/struct trackingdata *, component_t, const struct strlist */*filekeys*/, const char */*control*/);
retvalue binaries_checkadddeb(const struct deb_headers *, architecture_t /*forcearchitecture*/, packagetype_t, struct distribution *, bool tracking, component_t, bool /*permitnewerold*/);
#endif
