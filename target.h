#ifndef __MIRRORER_TARGET_H
#define __MIRRORER_TARGET_H

#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#endif

typedef struct s_target *target;

typedef retvalue get_name(target,const char *,char **);
typedef retvalue get_version(target,const char *,char **);
typedef retvalue get_installdata(target,const char *,const char *,const char *,char **,struct strlist *,struct strlist *);

struct s_target {
	char *codename;
	char *component;
	char *architecture;
	char *identifier;
	get_name *getname;
	get_version *getversion;
	get_installdata *getinstalldata;
};

retvalue target_initialize_binary(const char *distribution,const char *component,const char *architecture,target *target);
retvalue target_initialize_source(const char *distribution,const char *component,target *target);
void target_done(target target);

#endif
