#ifndef REPREPRO_DISTRIBUTION_H
#define REPREPRO_DISTRIBUTION_H

struct distribution;

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif
#ifndef REPREPRO_EXPORTS_H
#include "exports.h"
#endif
#ifndef REPREPRO_CONTENTS_H
#include "contents.h"
#endif

struct distribution {
	struct distribution *next;
	/* the primary name to access this distribution: */
	char *codename;
	/* additional information for the Release-file to be
	 * generated, may be NULL. only suite is sometimes used
	 * (and only for sanity checks) */
	/*@null@*/char *suite,*version;
	/*@null@*/char *origin,*label,*description,*notautomatic;
	/* What architectures and components are there */
	struct strlist architectures,components;
	/* which update rules to use */
	struct strlist updates;
	/* which rules to use to pull packages from other distributions */
	struct strlist pulls;
	/* the key to sign with, may be NULL: */
	/*@null@*/char *signwith;
	/* the override file to use by default */
	/*@null@*/char *deb_override,*udeb_override,*dsc_override;
	/* the list of components containing a debian-installer dir, normally only "main" */
	struct strlist udebcomponents;
	/* what kind of index files to generate */
	struct exportmode dsc,deb,udeb;
	/* is tracking enabled for this distribution? */
	enum trackingtype { dt_NONE=0, dt_KEEP, dt_ALL, dt_MINIMAL } tracking;
	struct trackingoptions { bool_t includechanges:1; 
		bool_t includebyhand:1;
		bool_t needsources:1;
		bool_t keepsources:1;
		bool_t embargoalls:1;
		} trackingoptions;
	/* what content files to generate */
	struct contentsoptions contents;
	/* A list of all targets contained in the distribution*/
	struct target *targets;
	/* a filename to look for who is allowed to upload packages */
	char *uploaders;
	/* RET_NOTHING: do not export with EXPORT_CHANGED, EXPORT_NEVER
	 * RET_OK: export unless EXPORT_NEVER
	 * RET_ERROR_*: only export with EXPORT_FORCE */
	retvalue status;
};


retvalue distribution_get(/*@out@*/struct distribution **distribution,const char *conf,const char *name);
retvalue distribution_free(/*@only@*/struct distribution *distribution);

typedef retvalue distribution_each_action(void *data, struct target *t, struct distribution *d);

/* call <action> for each part of <distribution>, if component or architecture is 
 * not NULL or "all", only do those parts */
retvalue distribution_foreach_part(struct distribution *distribution,/*@null@*/const char *component,/*@null@*/const char *architecture,/*@null@*/const char *packagetype,distribution_each_action action,/*@null@*/void *data);

/*@dependent@*/struct target *distribution_getpart(const struct distribution *distribution,const char *component,const char *architecture,const char *packagetype);

/* like distribtion_getpart, but returns NULL if there is no such target */
/*@dependent@*/struct target *distribution_gettarget(const struct distribution *distribution,const char *component,const char *architecture,const char *packagetype);

retvalue distribution_fullexport(struct distribution *distribution,const char *confdir,const char *dbdir,const char *distdir,filesdb);

enum exportwhen {EXPORT_NEVER, EXPORT_CHANGED, EXPORT_NORMAL, EXPORT_FORCE };
retvalue distribution_export(enum exportwhen when, struct distribution *distribution,const char *confdir,const char *dbdir,const char *distdir,filesdb);

/* get all dists from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_getmatched(const char *conf,int argc,const char *argv[],/*@out@*/struct distribution **distributions);

retvalue distribution_freelist(/*@only@*/struct distribution *distributions);
retvalue distribution_exportandfreelist(enum exportwhen when, /*@only@*/struct distribution *distributions,const char *confdir, const char *dbdir, const char *distdir, filesdb);
#endif
