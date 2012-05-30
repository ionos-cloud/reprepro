#ifndef REPREPRO_PULLS_H
#define REPREPRO_PULLS_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_RELEASE_H
#include "release.h"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

struct pull_rule;
struct pull_distribution;

retvalue pull_getrules(/*@out@*/struct pull_rule **);

void pull_freerules(/*@only@*/struct pull_rule *p);
void pull_freedistributions(/*@only@*/struct pull_distribution *p);

retvalue pull_prepare(struct distribution *, struct pull_rule *, bool fast, /*@null@*/const struct atomlist */*components*/,/*@null@*/const struct atomlist */*architectures*/,/*@null@*/const struct atomlist */*packagetypes*/, /*@out@*/struct pull_distribution **);
retvalue pull_update(struct pull_distribution *);
retvalue pull_checkupdate(struct pull_distribution *);
retvalue pull_dumpupdate(struct pull_distribution *);

#endif
