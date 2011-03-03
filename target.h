#ifndef REPREPRO_TARGET_H
#define REPREPRO_TARGET_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_NAMES_H
#include "names.h"
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

typedef retvalue get_name(const char *, /*@out@*/char **);
typedef retvalue get_version(const char *, /*@out@*/char **);
typedef retvalue get_installdata(const struct target *,const char *,const char *,const char *,/*@out@*/char **,/*@out@*/struct strlist *,/*@out@*/struct checksumsarray *,/*@null@*//*@out@*/enum filetype *);
/* md5sums may be NULL */
typedef retvalue get_filekeys(const char *, /*@out@*/struct strlist *);
typedef retvalue get_checksums(const char *, /*@out@*/struct checksumsarray *);
typedef char *get_upstreamindex(const char *suite_from, const char *component_from, const char *architecture);
typedef retvalue do_reoverride(const struct distribution *,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk);
typedef retvalue do_retrack(const char *packagename, const char *controlchunk, trackingdb, struct database *);
typedef retvalue get_sourceandversion(const char *chunk, const char *packagename, /*@out@*/char **source, /*@out@*/char **version);

struct target {
	char *codename;
	char *component;
	char *architecture;
	char *identifier;
	/* "deb" "udeb" or "dsc" */
	/*@observer@*/const char *packagetype;
	/* links into the correct description in distribution */
	/*@dependent@*/const struct exportmode *exportmode;
	/* the directory relative to <distdir>/<codename>/ to use */
	char *relativedirectory;
	/* functions to use on the packages included */
	get_name *getname;
	get_version *getversion;
	get_installdata *getinstalldata;
	get_filekeys *getfilekeys;
	get_checksums *getchecksums;
	get_upstreamindex *getupstreamindex;
	get_sourceandversion *getsourceandversion;
	do_reoverride *doreoverride;
	do_retrack *doretrack;
	bool wasmodified, saved_wasmodified;
	/* set when existed at startup time, only valid in --nofast mode */
	bool existed;
	/* the next one in the list of targets of a distribution */
	struct target *next;
	/* is initialized as soon as needed: */
	struct table *packages;
};

retvalue target_initialize_ubinary(const char *codename,const char *component,const char *architecture,/*@dependent@*/const struct exportmode *exportmode,/*@out@*/struct target **target);
retvalue target_initialize_binary(const char *codename,const char *component,const char *architecture,/*@dependent@*/const struct exportmode *exportmode,/*@out@*/struct target **target);
retvalue target_initialize_source(const char *codename,const char *component,/*@dependent@*/const struct exportmode *exportmode,/*@out@*/struct target **target);
retvalue target_free(struct target *target);

retvalue target_export(struct target *target, struct database *, bool onlyneeded, bool snapshot, struct release *release);

retvalue target_printmd5sums(const char *dirofdist,const struct target *target,FILE *out,int force);

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(struct target *target, struct database *, bool readonly);
/* this closes databases... */
retvalue target_closepackagesdb(struct target *target);

/* The following calls can only be called if target_initpackagesdb was called before: */
struct logger;
retvalue target_addpackage(struct target *, /*@null@*/struct logger *, struct database *, const char *name, const char *version, const char *control, const struct strlist *filekeys, /*@null@*/ bool *usedmarker, bool downgrade, /*@null@*/struct strlist *dereferencedfilekeys, /*@null@*/struct trackingdata *, enum filetype);
retvalue target_checkaddpackage(struct target *target, const char *name, const char *version, bool tracking, bool permitnewerold);
retvalue target_removepackage(struct target *, /*@null@*/struct logger *, struct database *, const char *name, /*@null@*/const char *oldpversion, /*@null@*/struct strlist *dereferencedfilekeys, struct trackingdata *);
/* like target_removepackage, but do not read control data yourself but use available */
retvalue target_removereadpackage(struct target *, /*@null@*/struct logger *, struct database *, const char *name, const char *oldcontrol, /*@null@*/const char *oldpversion, struct strlist *, /*@null@*/struct trackingdata *);
/* Like target_removepackage, but delete the package record by cursor */
retvalue target_removepackage_by_cursor(struct target *, /*@null@*/struct logger *, struct database *, struct cursor *, const char *name, const char *control, /*@null@*/const char *oldpversion, struct strlist *, /*@null@*/struct trackingdata *);

retvalue package_check(struct database *, struct distribution *, struct target *, const char *, const char *, void *);
retvalue target_rereference(struct target *, struct database *);
retvalue package_referenceforsnapshot(struct database *, struct distribution *, struct target *, const char *, const char *, void *);
retvalue target_reoverride(void *, struct target *, struct distribution *);

retvalue package_rerunnotifiers(struct database *, struct distribution *, struct target *, const char *, const char *, void *);

static inline bool target_matches(const struct target *t, const char *component, const char *architecture, const char *packagetype) {
	if( component != NULL && strcmp(component,t->component) != 0 )
		return false;
	if( architecture != NULL && strcmp(architecture,t->architecture) != 0 )
		return false;
	if( packagetype != NULL && strcmp(packagetype,t->packagetype) != 0 )
		return false;
	return true;
}
#endif
