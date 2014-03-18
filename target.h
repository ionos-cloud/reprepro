#ifndef REPREPRO_TARGET_H
#define REPREPRO_TARGET_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_NAMES_H
#include "names.h"
#endif
#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_TRACKINGT_H
#include "trackingt.h"
#endif
#ifndef REPREPRO_CHECKSUMS_H
#include "checksums.h"
#endif
#ifndef REPREPRO_EXPORTS_H
#include "exports.h"
#endif

struct target;
struct alloverrides;

typedef retvalue get_version(const char *, /*@out@*/char **);
typedef retvalue get_architecture(const char *, /*@out@*/architecture_t *);
typedef retvalue get_installdata(const struct target *, const char *, const char *, architecture_t, const char *, /*@out@*/char **, /*@out@*/struct strlist *, /*@out@*/struct checksumsarray *);
/* md5sums may be NULL */
typedef retvalue get_filekeys(const char *, /*@out@*/struct strlist *);
typedef retvalue get_checksums(const char *, /*@out@*/struct checksumsarray *);
typedef retvalue do_reoverride(const struct target *, const char * /*packagename*/, const char *, /*@out@*/char **);
typedef retvalue do_retrack(const char * /*packagename*/, const char * /*controlchunk*/, trackingdb);
typedef retvalue get_sourceandversion(const char *, const char * /*packagename*/, /*@out@*/char ** /*source_p*/, /*@out@*/char ** /*version_p*/);
typedef retvalue complete_checksums(const char *, const struct strlist *, struct checksums **, /*@out@*/char **);

struct distribution;
struct target {
	struct distribution *distribution;
	component_t component;
	architecture_t architecture;
	packagetype_t packagetype;
	char *identifier;
	/* links into the correct description in distribution */
	/*@dependent@*/const struct exportmode *exportmode;
	/* the directory relative to <distdir>/<codename>/ to use */
	char *relativedirectory;
	/* functions to use on the packages included */
	get_version *getversion;
	/* binary packages might be "all" or the architecture of the target */
	get_architecture *getarchitecture;
	get_installdata *getinstalldata;
	get_filekeys *getfilekeys;
	get_checksums *getchecksums;
	get_sourceandversion *getsourceandversion;
	do_reoverride *doreoverride;
	do_retrack *doretrack;
	complete_checksums *completechecksums;
	bool wasmodified, saved_wasmodified;
	/* set when existed at startup time, only valid in --nofast mode */
	bool existed;
	/* the next one in the list of targets of a distribution */
	struct target *next;
	/* is initialized as soon as needed: */
	struct table *packages;
	/* do not allow write operations */
	bool readonly;
	/* was updated without tracking data (no problem when distribution
	 * has no tracking, otherwise cause warning later) */
	bool staletracking;
};

retvalue target_initialize_ubinary(/*@dependant@*/struct distribution *, component_t, architecture_t, /*@dependent@*/const struct exportmode *, bool /*readonly*/, /*@NULL@*/const char *fakecomponentprefix, /*@out@*/struct target **);
retvalue target_initialize_binary(/*@dependant@*/struct distribution *, component_t, architecture_t, /*@dependent@*/const struct exportmode *, bool /*readonly*/, /*@NULL@*/const char *fakecomponentprefix, /*@out@*/struct target **);
retvalue target_initialize_source(/*@dependant@*/struct distribution *, component_t, /*@dependent@*/const struct exportmode *, bool /*readonly*/, /*@NULL@*/const char *fakecomponentprefix, /*@out@*/struct target **);
retvalue target_free(struct target *);

retvalue target_export(struct target *, bool /*onlyneeded*/, bool /*snapshot*/, struct release *);

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(struct target *, bool /*readonly*/);
/* this closes databases... */
retvalue target_closepackagesdb(struct target *);

struct target_cursor {
	/*@temp@*/struct target *target;
	struct cursor *cursor;
	const char *lastname;
	const char *lastcontrol;
};
#define TARGET_CURSOR_ZERO {NULL, NULL, NULL, NULL}
/* wrapper around initpackagesdb and table_newglobalcursor */
static inline retvalue target_openiterator(struct target *t, bool readonly, /*@out@*/struct target_cursor *tc) {
	retvalue r, r2;
	struct cursor *c;

	r = target_initpackagesdb(t, readonly);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r))
		return r;
	r = table_newglobalcursor(t->packages, &c);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		r2 = target_closepackagesdb(t);
		RET_UPDATE(r, r2);
		return r;
	}
	tc->target = t;
	tc->cursor = c;
	return RET_OK;
}
/* wrapper around cursor_nexttemp */
static inline bool target_nextpackage(struct target_cursor *tc, /*@out@*/const char **packagename_p, /*@out@*/const char **chunk_p) {
	bool success;
	success = cursor_nexttemp(tc->target->packages, tc->cursor,
			&tc->lastname, &tc->lastcontrol);
	if (success) {
		*packagename_p = tc->lastname;
		*chunk_p = tc->lastcontrol;
	} else {
		tc->lastname = NULL;
		tc->lastcontrol = NULL;
	}
	return success;
}
/* wrapper around cursor_nexttemp */
static inline bool target_nextpackage_len(struct target_cursor *tc, /*@out@*//*@null@*/const char **packagename_p, /*@out@*/const char **chunk_p, /*@out@*/size_t *len_p) {
	tc->lastname = NULL;
	tc->lastcontrol = NULL;
	return cursor_nexttempdata(tc->target->packages, tc->cursor,
			packagename_p, chunk_p, len_p);
}
/* wrapper around cursor_close and target_closepackagesdb */
static inline retvalue target_closeiterator(struct target_cursor *tc) {
	retvalue result, r;

	result = cursor_close(tc->target->packages, tc->cursor);
	r = target_closepackagesdb(tc->target);
	RET_UPDATE(result, r);
	return result;
}

/* The following calls can only be called if target_initpackagesdb was called before: */
struct logger;
struct description;
retvalue target_addpackage(struct target *, /*@null@*/struct logger *, const char *name, const char *version, const char *control, const struct strlist *filekeys, bool downgrade, /*@null@*/struct trackingdata *, architecture_t, /*@null@*/const char *causingrule, /*@null@*/const char *suitefrom, /*@null@*/struct description *);
retvalue target_checkaddpackage(struct target *, const char *name, const char *version, bool tracking, bool permitnewerold);
retvalue target_removepackage(struct target *, /*@null@*/struct logger *, const char *name, struct trackingdata *);
/* like target_removepackage, but do not read control data yourself but use available */
retvalue target_removereadpackage(struct target *, /*@null@*/struct logger *, const char *name, const char *oldcontrol, /*@null@*/struct trackingdata *);
/* Like target_removepackage, but delete the package record by cursor */
retvalue target_removepackage_by_cursor(struct target_cursor *, /*@null@*/struct logger *, /*@null@*/struct trackingdata *);

retvalue package_check(struct distribution *, struct target *, const char *, const char *, void *);
retvalue target_rereference(struct target *);
retvalue package_referenceforsnapshot(struct distribution *, struct target *, const char *, const char *, void *);
retvalue target_reoverride(struct target *, struct distribution *);
retvalue target_redochecksums(struct target *, struct distribution *);

retvalue package_rerunnotifiers(struct distribution *, struct target *, const char *, const char *, void *);

static inline bool target_matches(const struct target *t, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes) {
	if (limitations_missed(components, t->component))
		return false;
	if (limitations_missed(architectures, t->architecture))
		return false;
	if (limitations_missed(packagetypes, t->packagetype))
		return false;
	return true;
}
#endif
