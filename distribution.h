#ifndef __MIRRORER_DISTRIBUTION_H
#define __MIRRORER_DISTRIBUTION_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#endif
#ifndef __MIRRORER_TARGET_H
#include "target.h"
#endif

struct distribution {
	struct distribution *next;
	char *codename,*suite,*version;
	char *origin,*label,*description;
	struct strlist architectures,components,updates;
	/* the key to sign with, may be NULL: */
	char *signwith;
	/* the override file to use by default */
	char *override,*srcoverride;
	/* if it contains support for udebs, != NULL, normaly "debian-installer" */
	char *udeblistdir;
	/* A list of all targets contained in the distribution*/
	struct target *targets;
	/* list of all update_upstreams for this ditribution,
	 * only set when update_getupstreams was called for this*/
	struct update_origin *updateorigins;
	struct update_target *updatetargets;
};


retvalue distribution_get(struct distribution **distribution,const char *conf,const char *name);
retvalue distribution_free(struct distribution *distribution);

typedef retvalue distribution_each_action(void *data, struct target *t);

/* call <action> for each part of <distribution>, if component or architecture is 
 * not NULL or "all", only do those parts */
retvalue distribution_foreach_part(const struct distribution *distribution,const char *component,const char *architecture,distribution_each_action action,void *data,int force);

struct target *distribution_getpart(const struct distribution *distribution,const char *component,const char *architecture);

retvalue distribution_export(struct distribution *distribution,const char *dbdir,const char *distdir,int force,int onlyneeded);

typedef retvalue distributionaction(void *data,const char *chunk,struct distribution *distribution);

/* call <action> for each distribution-chunk from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_foreach(const char *conf,int argc,const char *argv[],distributionaction action,void *data,int force);

/* get all dists from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_getmatched(const char *conf,int argc,const char *argv[],struct distribution **distributions);

#endif
