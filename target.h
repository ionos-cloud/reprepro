#ifndef REPREPRO_TARGET_H
#define REPREPRO_TARGET_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_NAMES_H
#include "names.h"
#endif
#ifndef REPREPRO_TRACKINGT_H
#include "trackingt.h"
#endif
#ifndef REPREPRO_PACKAGES_H
#include "packages.h"
#endif
#ifndef REPREPRO_EXPORTS_H
#include "exports.h"
#endif

struct target;
struct alloverrides;

typedef retvalue get_name(struct target *,const char *,/*@out@*/char **);
typedef retvalue get_version(struct target *,const char *,/*@out@*/char **);
typedef retvalue get_installdata(struct target *,const char *,const char *,const char *,/*@out@*/char **,/*@out@*/struct strlist *,/*@out@*/struct strlist *,/*@out@*/struct strlist *);
/* md5sums may be NULL */
typedef retvalue get_filekeys(struct target *,const char *,/*@out@*/struct strlist *filekeys,/*@out@*/struct strlist *md5sum);
typedef char *get_upstreamindex(struct target *,const char *suite_from,
		const char *component_from,const char *architecture);
typedef retvalue do_reoverride(const struct alloverrides *,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk);
typedef retvalue do_retrack(struct target *,const char *packagename,const char *controlchunk,trackingdb,references);
typedef retvalue get_sourceandversion(struct target *,const char *chunk,const char *packagename,char **source,char **version);

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
	get_upstreamindex *getupstreamindex;
	get_sourceandversion *getsourceandversion;
	do_reoverride *doreoverride;
	do_retrack *doretrack;
	bool_t wasmodified;
	/* the next one in the list of targets of a distribution */
	struct target *next;
	/* is initialized as soon as needed: */
	packagesdb packages;
};

retvalue target_initialize_ubinary(const char *codename,const char *component,const char *architecture,/*@dependent@*/const struct exportmode *exportmode,/*@out@*/struct target **target);
retvalue target_initialize_binary(const char *codename,const char *component,const char *architecture,/*@dependent@*/const struct exportmode *exportmode,/*@out@*/struct target **target);
retvalue target_initialize_source(const char *codename,const char *component,/*@dependent@*/const struct exportmode *exportmode,/*@out@*/struct target **target);
retvalue target_free(struct target *target);

retvalue target_mkdistdir(struct target *target,const char *distdir);
retvalue target_export(struct target *target,const char *confdir,const char *dbdir,const char *dirofdist,int force,bool_t onlyneeded, struct strlist *releasedfiles );

retvalue target_printmd5sums(const char *dirofdist,const struct target *target,FILE *out,int force);

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(struct target *target, const char *dbdir);
/* this closes databases... */
retvalue target_closepackagesdb(struct target *target);

/* The following calls can only be called if target_initpackagesdb was called before: */

retvalue target_addpackage(struct target *target,references refs,const char *name,const char *version,const char *control,const struct strlist *filekeys,int force,bool_t downgrade,/*@null@*/struct strlist *dereferencedfilekeys,/*@null@*/struct trackingdata *,enum filetype);
retvalue target_removepackage(struct target *target,references refs,const char *name, /*@null@*/struct strlist *dereferencedfilekeys,struct trackingdata *);
retvalue target_writeindices(const char *dirofdist,struct target *target,int force,bool_t onlyneeded);
retvalue target_check(struct target *target,filesdb filesdb,references refsdb,int force);
retvalue target_rereference(struct target *target,references refs,int force);
retvalue target_retrack(struct target *target,trackingdb tracks,references refs,int force);
retvalue target_reoverride(struct target *target,const struct alloverrides *ao);

#endif
