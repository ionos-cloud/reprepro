
#include "packages.h"

/* Things for making decisions what to upgrade and what not */

typedef enum { UD_NO, UD_UPGRADE, UD_HOLD } upgrade_decision;

typedef upgrade_decision upgrade_decide_function(const char *package,const char *old_version,const char *new_version);

upgrade_decision ud_always(const char *p,const char *ov,const char *nv);

/* The main part: */

typedef struct s_upgradelist *upgradelist;

retvalue upgradelist_initialize(upgradelist *ul,packagesdb packages,upgrade_decide_function *decide);
retvalue upgradelist_done(upgradelist upgrade);

retvalue upgradelist_update(upgradelist upgrade,const char *filename,int force);
