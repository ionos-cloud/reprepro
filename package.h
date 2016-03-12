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
struct logger;
struct trackingdata;


retvalue package_get(struct target *, const char * /*name*/, /*@null@*/ const char */*version*/, /*@out@*/ struct package *);

static inline void package_done(struct package *pkg) {
	free(pkg->pkgname);
	free(pkg->pkgchunk);
	memset(pkg, 0, sizeof(*pkg));
}


struct package_cursor {
	/*@temp@*/struct target *target;
	struct cursor *cursor;
	struct package current;
};

retvalue package_openiterator(struct target *, bool /*readonly*/, /*@out@*/struct package_cursor *);
bool package_next(struct package_cursor *);
retvalue package_closeiterator(struct package_cursor *);

retvalue package_remove_by_cursor(struct package_cursor *, /*@null@*/struct logger *, /*@null@*/struct trackingdata *);
retvalue package_newcontrol_by_cursor(struct package_cursor *, const char *, size_t);

#endif
