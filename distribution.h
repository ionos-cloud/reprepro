#ifndef REPREPRO_DISTRIBUTION_H
#define REPREPRO_DISTRIBUTION_H

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

struct distribution {
	struct distribution *next;
	/* the primary name to access this distribution: */
	char *codename;
	/* additional information for the Release-file to be
	 * generated, may be NULL. only suite is sometimes used
	 * (and only for sanity checks) */
	/*@null@*/char *suite,*version;
	/*@null@*/char *origin,*label,*description;
	/* What architectures and components are there */
	struct strlist architectures,components;
	/* which update rules to use */
	struct strlist updates;
	/* the key to sign with, may be NULL: */
	/*@null@*/char *signwith;
	/* the override file to use by default */
	/*@null@*/char *override,*srcoverride;
	/* the list of components containing a debian-installer dir, normaly only "main" */
	struct strlist udebcomponents;
	/* what kind of index files to generate */
	struct exportmode dsc,deb,udeb;
	/* A list of all targets contained in the distribution*/
	struct target *targets;
};


retvalue distribution_get(/*@out@*/struct distribution **distribution,const char *conf,const char *name);
retvalue distribution_free(/*@only@*/struct distribution *distribution);

typedef retvalue distribution_each_action(void *data, struct target *t);

/* call <action> for each part of <distribution>, if component or architecture is 
 * not NULL or "all", only do those parts */
retvalue distribution_foreach_part(const struct distribution *distribution,/*@null@*/const char *component,/*@null@*/const char *architecture,/*@null@*/const char *packagetype,distribution_each_action action,/*@null@*/void *data,int force);

struct target *distribution_getpart(const struct distribution *distribution,const char *component,const char *architecture,const char *packagetype);

retvalue distribution_export(struct distribution *distribution,const char *dbdir,const char *distdir,int force,bool_t onlyneeded);

/* get all dists from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_getmatched(const char *conf,int argc,const char *argv[],/*@out@*/struct distribution **distributions);

retvalue distribution_freelist(/*@only@*/struct distribution *distributions);
retvalue distribution_exportandfreelist(/*@only@*/struct distribution *distributions, const char *dbdir, const char *distdir, int force);
#endif
