#ifndef __MIRRORER_UPGRADELIST_H
#define __MIRRORER_UPGRADELIST_H

#ifndef __MIRRORER_PACKAGES_H
#include "packages.h"
#endif
#ifndef __MIRRORER_APTMETHOD_H
#include "aptmethod.h"
#endif
#ifndef __MIRRORER_DOWNLOADCACHE_H
#include "downloadcache.h"
#endif

/* Things for making decisions what to upgrade and what not */

typedef enum { UD_NO, UD_UPGRADE, UD_HOLD } upgrade_decision;

typedef upgrade_decision upgrade_decide_function(const char *package,const char *old_version,const char *new_version,const char *newcontrolchunk);

upgrade_decision ud_always(const char *p,const char *ov,const char *nv,const char *nc);

/* The main part: */

struct target;
struct upgradelist;

retvalue upgradelist_initialize(struct upgradelist **ul,struct target *target,const char *dbdir,upgrade_decide_function *decide);
retvalue upgradelist_free(struct upgradelist *upgrade);

retvalue upgradelist_dump(struct upgradelist *upgrade);
retvalue upgradelist_listmissing(struct upgradelist *upgrade,filesdb files);

/* Take all items in 'filename' into account, and remember them coming from 'method' */
retvalue upgradelist_update(struct upgradelist *upgrade,struct aptmethod *method,const char *filename,int force);

//TODO add a function to reduce data-load by removing anything not needed
//any longer. (perhaps with a flag to remove all packages that are no
//longer available upstream)

/* request all wanted files refering the methods given before */
retvalue upgradelist_enqueue(struct upgradelist *upgrade,struct downloadcache *cache,filesdb filesdb,int force);

retvalue upgradelist_install(struct upgradelist *upgrade,const char *dbdir,filesdb files,DB *references,int force);

#endif
