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
	char *codename,*suite,*version;
	char *origin,*label,*description;
	struct strlist architectures,components;
};


retvalue distribution_get(struct distribution **distribution,const char *conf,const char *name);
void distribution_free(struct distribution *distribution);

typedef retvalue distribution_each_source_action(void *data, const char *component);
typedef retvalue distribution_each_binary_action(void *data, const char *component, const char *arch);
typedef retvalue distribution_each_action(void *data, const target t);

/* call <action> for each part of <distribution>. */
retvalue distribution_foreach_part(const struct distribution *distribution,distribution_each_action action,void *data,int force);

typedef retvalue distributionaction(void *data,const char *chunk,const struct distribution *distribution);

/* call <action> for each distribution-chunk from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_foreach(const char *conf,int argc,char *argv[],distributionaction action,void *data,int force);

#endif
