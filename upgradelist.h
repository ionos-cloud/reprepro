#ifndef __MIRRORER_UPGRADELIST_H
#define __MIRRORER_UPGRADELIST_H

#ifndef __MIRRORER_PACKAGES_H
#include "packages.h"
#endif
#ifndef __MIRRORER_DOWNLOADLIST_H
#include "downloadlist.h"
#endif

/* Things for making decisions what to upgrade and what not */

typedef enum { UD_NO, UD_UPGRADE, UD_HOLD } upgrade_decision;

typedef upgrade_decision upgrade_decide_function(const char *package,const char *old_version,const char *new_version);

upgrade_decision ud_always(const char *p,const char *ov,const char *nv);

/* The main part: */

struct target;
struct upgradelist;

retvalue upgradelist_initialize(struct upgradelist **ul,struct target *target,const char *dbdir,upgrade_decide_function *decide);
retvalue upgradelist_free(struct upgradelist *upgrade);

retvalue upgradelist_dump(struct upgradelist *upgrade);
retvalue upgradelist_listmissing(struct upgradelist *upgrade,filesdb files);

/* Take all items in 'filename' into account, and remember them coming from 'upstream */
retvalue upgradelist_update(struct upgradelist *upgrade,struct download_upstream *upstream,const char *filename,int force);

//TODO add a function to reduce data-load by removing anything not needed
//any longer. (perhaps with a flag to remove all packages that are no
//longer available upstream)

/* request all wanted files in the downloadlists given before */
retvalue upgradelist_enqueue(struct upgradelist *upgrade,int force);

retvalue upgradelist_install(struct upgradelist *upgrade,filesdb files,DB *references,int force);

#endif
