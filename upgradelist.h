#ifndef __MIRRORER_UPGRADELIST_H
#define __MIRRORER_UPGRADELIST_H

#ifndef __MIRRORER_PACKAGES_H
#include "packages.h"
#endif
#ifndef __MIRRORER_TARGET_H
#include "target.h"
#endif

/* Things for making decisions what to upgrade and what not */

typedef enum { UD_NO, UD_UPGRADE, UD_HOLD } upgrade_decision;

typedef upgrade_decision upgrade_decide_function(const char *package,const char *old_version,const char *new_version);

upgrade_decision ud_always(const char *p,const char *ov,const char *nv);

/* The main part: */

typedef struct s_upgradelist *upgradelist;

retvalue upgradelist_initialize(upgradelist *ul,target target, packagesdb packages,upgrade_decide_function *decide);
retvalue upgradelist_done(upgradelist upgrade);

retvalue upgradelist_dump(upgradelist upgrade);
retvalue upgradelist_listmissing(upgradelist upgrade,filesdb files);

retvalue upgradelist_update(upgradelist upgrade,const char *filename,int force);
retvalue upgradelist_install(upgradelist upgrade,filesdb files,DB *references,int force);

#endif
