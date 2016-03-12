#ifndef REPREPRO_PACKAGE_H
#define REPREPRO_PACKAGE_H

struct package {
	/*@temp@*/ struct target *target;
	const char *name;
	const char *control;
	size_t controllen;

	/* used to keep the memory that might be needed for the above,
	 * only to be used to free once this struct is abandoned */
	char *pkgchunk, *pkgname;
};
struct target;


retvalue package_get(struct target *, const char * /*name*/, /*@null@*/ const char */*version*/, /*@out@*/ struct package *);

static inline void package_done(struct package *pkg) {
	free(pkg->pkgname);
	free(pkg->pkgchunk);
	memset(pkg, 0, sizeof(*pkg));
}

#endif
