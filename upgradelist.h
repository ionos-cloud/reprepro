#ifndef REPREPRO_UPGRADELIST_H
#define REPREPRO_UPGRADELIST_H

/* Things for making decisions what to upgrade and what not */

typedef enum { UD_ERROR, UD_LOUDNO, UD_NO, UD_UPGRADE, UD_HOLD, UD_SUPERSEDE } upgrade_decision;

struct package;
typedef upgrade_decision upgrade_decide_function(void *privdata, struct target *, struct package *, /*@null@*/ const char */*oldversion*/);

/* The main part: */

struct target;
struct logger;
struct upgradelist;

retvalue upgradelist_initialize(struct upgradelist **, /*@dependent@*/struct target *);
void upgradelist_free(/*@only@*/struct upgradelist *);

typedef void dumpaction(const char */*packagename*/, /*@null@*/const char */*oldversion*/, /*@null@*/const char */*newversion*/, /*@null@*/const char */*bestcandidate*/, /*@null@*/const struct strlist */*newfilekeys*/, /*@null@*/const char */*newcontrol*/, void *);

void upgradelist_dump(struct upgradelist *, dumpaction *);

/* Take all items in 'filename' into account, and remember them coming from 'method' */
retvalue upgradelist_update(struct upgradelist *, /*@dependent@*/void *, const char * /*filename*/, upgrade_decide_function *, void *, bool /*ignorewrongarchitecture*/);

/* Take all items in source into account */
retvalue upgradelist_pull(struct upgradelist *, struct target *, upgrade_decide_function *, void *, void *);

/* mark all packages as deleted, so they will vanis unless readded or reholded */
retvalue upgradelist_deleteall(struct upgradelist *);

typedef retvalue enqueueaction(void *, const struct checksumsarray *, const struct strlist *, void *);
/* request all wanted files refering the methods given before */
retvalue upgradelist_enqueue(struct upgradelist *, enqueueaction *, void *);

bool upgradelist_isbigdelete(const struct upgradelist *);
bool upgradelist_woulddelete(const struct upgradelist *);

retvalue upgradelist_install(struct upgradelist *, /*@null@*/struct logger *, bool /*ignoredelete*/, void (*)(void *, const char **, const char **));

/* remove all packages that would either be removed or upgraded by an upgrade */
retvalue upgradelist_predelete(struct upgradelist *, /*@null@*/struct logger *);

#endif
