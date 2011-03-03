#ifndef REPREPRO_UPGRADELIST_H
#define REPREPRO_UPGRADELIST_H

#ifndef REPREPRO_PACKAGES_H
#include "packages.h"
#endif
#ifndef REPREPRO_APTMETHOD_H
#include "aptmethod.h"
#endif
#ifndef REPREPRO_DOWNLOADCACHE_H
#include "downloadcache.h"
#endif

/* Things for making decisions what to upgrade and what not */

typedef enum { UD_ERROR, UD_NO, UD_UPGRADE, UD_HOLD } upgrade_decision;

typedef upgrade_decision upgrade_decide_function(void *privdata, const char *package,const char *old_version,const char *new_version,const char *newcontrolchunk);

upgrade_decision ud_always(void *privdata, const char *p,const char *ov,const char *nv,const char *nc);

/* The main part: */

struct target;
struct upgradelist;

retvalue upgradelist_initialize(struct upgradelist **ul,/*@dependent@*/struct target *target,const char *dbdir);
retvalue upgradelist_free(/*@only@*/struct upgradelist *upgrade);

void upgradelist_dump(struct upgradelist *upgrade);
retvalue upgradelist_listmissing(struct upgradelist *upgrade,filesdb files);

/* Take all items in 'filename' into account, and remember them coming from 'method' */
retvalue upgradelist_update(struct upgradelist *upgrade,/*@dependent@*/struct aptmethod *method,const char *filename,upgrade_decide_function *predecide,void *decide_data);

/* Take all items in source into account */
retvalue upgradelist_pull(struct upgradelist *upgrade,struct target *source,upgrade_decide_function *predecide,void *decide_data,const char *dbdir);

/* mark all packages as deleted, so they will vanis unless readded or reholded */
retvalue upgradelist_deleteall(struct upgradelist *upgrade);

//TODO add a function to reduce data-load by removing anything not needed
//any longer. (perhaps with a flag to remove all packages that are no
//longer available upstream)

/* request all wanted files refering the methods given before */
retvalue upgradelist_enqueue(struct upgradelist *upgrade,struct downloadcache *cache,filesdb filesdb);

retvalue upgradelist_install(struct upgradelist *upgrade,const char *dbdir,filesdb files,references refs, bool_t ignoredelete, struct strlist *dereferencedfilekeys);

/* remove all packages that would either be removed or upgraded by an upgrade */
retvalue upgradelist_predelete(struct upgradelist *upgrade,const char *dbdir,references refs,struct strlist *dereferencedfilekeys);

#endif
