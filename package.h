#ifndef REPREPRO_PACKAGE_H
#define REPREPRO_PACKAGE_H

#include "atoms.h"

struct package {
	/*@temp@*/ struct target *target;
	const char *name;
	const char *control;
	size_t controllen;
	/* for the following NULL means not yet extracted: */
	const char *version;
	const char *source;
	const char *sourceversion;
	architecture_t architecture;

	/* used to keep the memory that might be needed for the above,
	 * only to be used to free once this struct is abandoned */
	char *pkgchunk, *pkgname, *pkgversion, *pkgsource, *pkgsrcversion;
};
struct distribution;
struct target;
struct atomlist;
struct logger;
struct trackingdata;

typedef retvalue action_each_target(struct target *, void *);
typedef retvalue action_each_package(struct package *, void *);

/* call <action> for each package of <distribution> */
retvalue package_foreach(struct distribution *, /*@null@*/const struct atomlist *, /*@null@*/const struct atomlist *, /*@null@*/const struct atomlist *, action_each_package, /*@null@*/action_each_target, void *);
/* same but different ways to restrict it */
retvalue package_foreach_c(struct distribution *, /*@null@*/const struct atomlist *, architecture_t, packagetype_t, action_each_package, void *);

/* delete every package decider returns RET_OK for */
retvalue package_remove_each(struct distribution *, const struct atomlist *, const struct atomlist *, const struct atomlist *, action_each_package /*decider*/, struct trackingdata *, void *);


retvalue package_get(struct target *, const char * /*name*/, /*@null@*/ const char */*version*/, /*@out@*/ struct package *);

static inline void package_done(struct package *pkg) {
	free(pkg->pkgname);
	free(pkg->pkgchunk);
	free(pkg->pkgversion);
	free(pkg->pkgsource);
	free(pkg->pkgsrcversion);
	memset(pkg, 0, sizeof(*pkg));
}

retvalue package_getversion(struct package *);
retvalue package_getsource(struct package *);
retvalue package_getarchitecture(struct package *);

static inline char *package_dupversion(struct package *package) {
	assert (package->version != NULL);
	if (package->pkgversion == NULL)
		return strdup(package->version);
	else {
		// steal version from package
		// (caller must ensure it is not freed while still needed)
		char *v = package->pkgversion;
		package->pkgversion = NULL;
		return v;
	}
}

struct package_cursor {
	/*@temp@*/struct target *target;
	struct cursor *cursor;
	struct package current;
	bool close_database;
};

retvalue package_openiterator(struct target *, bool /*readonly*/, bool /*duplicate*/, /*@out@*/struct package_cursor *);
retvalue package_openduplicateiterator(struct target *t, const char *name, long long, /*@out@*/struct package_cursor *tc);
bool package_next(struct package_cursor *);
retvalue package_closeiterator(struct package_cursor *);

retvalue package_remove(struct package *, /*@null@*/struct logger *, /*@null@*/struct trackingdata *);
retvalue package_remove_by_cursor(struct package_cursor *, /*@null@*/struct logger *, /*@null@*/struct trackingdata *);
retvalue package_newcontrol_by_cursor(struct package_cursor *, const char *, size_t);

retvalue package_check(struct package *, void *);
retvalue package_referenceforsnapshot(struct package *, void *);
retvalue package_rerunnotifiers(struct package *, void *);

#endif
