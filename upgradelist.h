#ifndef REPREPRO_UPGRADELIST_H
#define REPREPRO_UPGRADELIST_H

/* Things for making decisions what to upgrade and what not */

typedef enum { UD_ERROR, UD_LOUDNO, UD_NO, UD_UPGRADE, UD_HOLD } upgrade_decision;

typedef upgrade_decision upgrade_decide_function(void *privdata, const struct target *, const char *package, const char *old_version, const char *new_version, const char *newcontrolchunk);

upgrade_decision ud_always(void *, const struct target *, const char *, const char *, const char *, const char *);

/* The main part: */

struct target;
struct logger;
struct upgradelist;

retvalue upgradelist_initialize(struct upgradelist **ul,/*@dependent@*/struct target *target,struct database *);
void upgradelist_free(/*@only@*/struct upgradelist *upgrade);

typedef void dumpaction(const char */*packagename*/, /*@null@*/const char */*oldversion*/, /*@null@*/const char */*newversion*/, /*@null@*/const char */*bestcandidate*/, /*@null@*/const struct strlist */*newfilekeys*/, /*@null@*/const char */*newcontrol*/, void *);

void upgradelist_dump(struct upgradelist *upgrade, dumpaction *action);
retvalue upgradelist_listmissing(struct upgradelist *upgrade,struct database *);

/* Take all items in 'filename' into account, and remember them coming from 'method' */
retvalue upgradelist_update(struct upgradelist *upgrade, /*@dependent@*/void *, const char *filename, upgrade_decide_function *predecide, void *decide_data, bool ignorewrongarchitecture);

/* Take all items in source into account */
retvalue upgradelist_pull(struct upgradelist *, struct target *, upgrade_decide_function *, void *, struct database *, void *);

/* mark all packages as deleted, so they will vanis unless readded or reholded */
retvalue upgradelist_deleteall(struct upgradelist *upgrade);

typedef retvalue enqueueaction(void *, struct database *, const struct checksumsarray *, const struct strlist *, void *);
/* request all wanted files refering the methods given before */
retvalue upgradelist_enqueue(struct upgradelist *, enqueueaction *, void *, struct database *);

bool upgradelist_isbigdelete(const struct upgradelist *);

retvalue upgradelist_install(struct upgradelist *upgrade, /*@null@*/struct logger *, struct database *, bool ignoredelete, void (*callback)(void *, const char **, const char **));

/* remove all packages that would either be removed or upgraded by an upgrade */
retvalue upgradelist_predelete(struct upgradelist *, /*@null@*/struct logger *, struct database *);

#endif
