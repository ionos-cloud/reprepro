#ifndef __MIRRORER_UPDATES_H
#define __MIRRORER_UPDATES_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

#include "release.h"
#include "strlist.h"


struct update {
	char *name;
	struct strlist architectures;
	char *suite_from;
	struct strlist components_from;
	struct strlist components_into;
};

typedef retvalue updatesaction(void *data,const char *chunk,const struct release *release,struct update *update);

retvalue updates_foreach(const char *confdir,int argc,char *argv[],updatesaction action,void *data,int force);


#endif
