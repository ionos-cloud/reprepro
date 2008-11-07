#ifndef REPREPRO_UPGRADELIST_H
#define REPREPRO_UPGRADELIST_H

/* Things for making decisions what to upgrade and what not */

typedef enum { UD_ERROR, UD_NO, UD_UPGRADE, UD_HOLD } upgrade_decision;

typedef upgrade_decision upgrade_decide_function(void *privdata, const char *package,const char *old_version,const char *new_version,const char *newcontrolchunk);

upgrade_decision ud_always(void *privdata, const char *p,const char *ov,const char *nv,const char *nc);

/* The main part: */

struct target;
struct logger;
struct upgradelist;

retvalue upgradelist_initialize(struct upgradelist **ul,/*@dependent@*/struct target *target,struct database *);
void upgradelist_free(/*@only@*/struct upgradelist *upgrade);

void upgradelist_dump(struct upgradelist *upgrade);
retvalue upgradelist_listmissing(struct upgradelist *upgrade,struct database *);

/* Take all items in 'filename' into account, and remember them coming from 'method' */
retvalue upgradelist_update(struct upgradelist *upgrade, /*@dependent@*/void *, const char *filename, upgrade_decide_function *predecide, void *decide_data, bool ignorewrongarchitecture);

/* Take all items in source into account */
retvalue upgradelist_pull(struct upgradelist *upgrade,struct target *source,upgrade_decide_function *predecide,void *decide_data,struct database *);

/* mark all packages as deleted, so they will vanis unless readded or reholded */
retvalue upgradelist_deleteall(struct upgradelist *upgrade);

typedef retvalue enqueueaction(void *, struct database *, const struct checksumsarray *, const struct strlist *, void *);
/* request all wanted files refering the methods given before */
retvalue upgradelist_enqueue(struct upgradelist *, enqueueaction *, void *, struct database *);

retvalue upgradelist_install(struct upgradelist *upgrade, /*@null@*/struct logger *, struct database *, bool ignoredelete);

/* remove all packages that would either be removed or upgraded by an upgrade */
retvalue upgradelist_predelete(struct upgradelist *, /*@null@*/struct logger *, struct database *);

#endif
