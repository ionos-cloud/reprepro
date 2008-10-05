#ifndef REPREPRO_REMOTEREPOSITORY_H
#define REPREPRO_REMOTEREPOSITORY_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_APTMETHOD_H
#include "aptmethod.h"
#endif
#ifndef REPREPRO_DONEFILE_H
#include "donefile.h"
#endif
#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif

struct remote_repository;
struct remote_distribution;
struct remote_index;

/* register repository, strings as stored by reference */
struct remote_repository *remote_repository_prepare(const char *name, const char *method, const char *fallback, const struct strlist *config);

/* register remote distribution of the given repository */
struct remote_distribution *remote_distribution_prepare(struct remote_repository *, const char *suite, bool ignorerelease, const char *verifyrelease, bool flat, bool *ignorehashes);

void remote_repository_free(/*@only@*/struct remote_repository *);

/* create aptmethods for all of yet created repositories */
retvalue remote_startup(struct aptmethodrun *);

retvalue remote_preparemetalists(struct aptmethodrun *, bool nodownload);
retvalue remote_preparelists(struct aptmethodrun *, bool nodownload);

struct remote_index *remote_index(struct remote_distribution *, const char *architecture, const char *component, packagetype_t);
struct remote_index *remote_flat_index(struct remote_distribution *, packagetype_t);

/* returns the name of the prepared uncompressed file */
/*@observer@*/const char *remote_index_file(const struct remote_index*);
/*@observer@*/const char *remote_index_basefile(const struct remote_index*);
/*@observer@*/struct aptmethod *remote_aptmethod(const struct remote_distribution *);

bool remote_index_isnew(const struct remote_index *, struct donefile *);
void remote_index_needed(struct remote_index *);
void remote_index_markdone(const struct remote_index *, struct markdonefile *);

char *genlistsfilename(/*@null@*/const char * /*type*/, unsigned int /*count*/, ...) __attribute__((sentinel));
#endif
