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
struct package;
typedef retvalue get_installdata(const struct target *, struct package *, /*@out@*/char **, /*@out@*/struct strlist *, /*@out@*/struct checksumsarray *);
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
	/* has noexport option */
	bool noexport;
	/* was updated without tracking data (no problem when distribution
	 * has no tracking, otherwise cause warning later) */
	bool staletracking;
};

retvalue target_initialize_ubinary(/*@dependant@*/struct distribution *, component_t, architecture_t, /*@dependent@*/const struct exportmode *, bool /*readonly*/, bool /*noexport*/, /*@NULL@*/const char *fakecomponentprefix, /*@out@*/struct target **);
retvalue target_initialize_dbinary(/*@dependant@*/struct distribution *, component_t, architecture_t, /*@dependent@*/const struct exportmode *, bool /*readonly*/, bool /*noexport*/, /*@NULL@*/const char *fakecomponentprefix, /*@out@*/struct target **);
retvalue target_initialize_binary(/*@dependant@*/struct distribution *, component_t, architecture_t, /*@dependent@*/const struct exportmode *, bool /*readonly*/, bool /*noexport*/, /*@NULL@*/const char *fakecomponentprefix, /*@out@*/struct target **);
retvalue target_initialize_source(/*@dependant@*/struct distribution *, component_t, /*@dependent@*/const struct exportmode *, bool /*readonly*/, bool /*noexport*/, /*@NULL@*/const char *fakecomponentprefix, /*@out@*/struct target **);
retvalue target_free(struct target *);

retvalue target_export(struct target *, bool /*onlyneeded*/, bool /*snapshot*/, struct release *);

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(struct target *, bool /*readonly*/);
/* this closes databases... */
retvalue target_closepackagesdb(struct target *);

/* The following calls can only be called if target_initpackagesdb was called before: */
struct logger;
struct description;
retvalue target_addpackage(struct target *, /*@null@*/struct logger *, const char *name, const char *version, const char *control, const struct strlist *filekeys, bool downgrade, /*@null@*/struct trackingdata *, architecture_t, /*@null@*/const char *causingrule, /*@null@*/const char *suitefrom);
retvalue target_checkaddpackage(struct target *, const char *name, const char *version, bool tracking, bool permitnewerold);
retvalue target_removepackage(struct target *, /*@null@*/struct logger *, const char *name, const char *version, struct trackingdata *);
/* like target_removepackage, but do not read control data yourself but use available */
retvalue target_rereference(struct target *);
retvalue target_reoverride(struct target *, struct distribution *);
retvalue target_redochecksums(struct target *, struct distribution *);

static inline bool target_matches(const struct target *t, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes) {
	if (limitations_missed(components, t->component))
		return false;
	if (limitations_missed(architectures, t->architecture))
		return false;
	if (limitations_missed(packagetypes, t->packagetype))
		return false;
	return true;
}

static inline char *package_primarykey(const char *packagename, const char *version) {
	char *key;

	assert (packagename != NULL);
	assert (version != NULL);
	key = malloc(strlen(packagename) + 1 + strlen(version) + 1);
	if (key != NULL) {
		strcpy(key, packagename);
		strcat(key, "|");
		strcat(key, version);
	}
	return key;
}
#endif
