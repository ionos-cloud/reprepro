#ifndef __MIRRORER_DISTRIBUTION_H
#define __MIRRORER_DISTRIBUTION_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#include "strlist.h"

struct distribution {
	char *codename,*suite,*version;
	char *origin,*label,*description;
	struct strlist architectures,components;
};

// void distribution_free(struct distribution *distribution);

typedef retvalue distribution_each_source_action(void *data, const char *component);
typedef retvalue distribution_each_binary_action(void *data, const char *component, const char *arch);

/* call <sourceaction> for each source part of <distribution> and <binaction> for each binary part of it. */
retvalue distribution_foreach_part(const struct distribution *distribution,distribution_each_source_action sourceaction,distribution_each_binary_action binaction,void *data);


typedef retvalue distributionaction(void *data,const char *chunk,const struct distribution *distribution);

/* call <action> for each distribution-chunk from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_foreach(const char *conf,int argc,char *argv[],distributionaction action,void *data,int force);

#endif
