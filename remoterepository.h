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
struct remote_repository *remote_repository_prepare(const char * /*name*/, const char * /*method*/, const char * /*fallback*/, const struct strlist * /*config*/);

/* register remote distribution of the given repository */
retvalue remote_distribution_prepare(struct remote_repository *, const char * /*suite*/, bool /*ignorerelease*/, bool /*getinrelease*/, const char * /*verifyrelease*/, bool /*flat*/, bool * /*ignorehashes*/, /*@out@*/struct remote_distribution **);

void remote_repository_free(/*@only@*/struct remote_repository *);

/* create aptmethods for all of yet created repositories */
retvalue remote_startup(struct aptmethodrun *);

retvalue remote_preparemetalists(struct aptmethodrun *, bool /*nodownload*/);
retvalue remote_preparelists(struct aptmethodrun *, bool /*nodownload*/);

struct encoding_preferences {
	/* number of preferences, 0 means use default */
	unsigned short count;
	/* a list of compressions to use */
	struct compression_preference {
		bool diff;
		bool force;
		enum compression compression;
	} requested[3*c_COUNT];
};

struct remote_index *remote_index(struct remote_distribution *, const char * /*architecture*/, const char * /*component*/, packagetype_t, const struct encoding_preferences *);
struct remote_index *remote_flat_index(struct remote_distribution *, packagetype_t, const struct encoding_preferences *);

/* returns the name of the prepared uncompressed file */
/*@observer@*/const char *remote_index_file(const struct remote_index *);
/*@observer@*/const char *remote_index_basefile(const struct remote_index *);
/*@observer@*/struct aptmethod *remote_aptmethod(const struct remote_distribution *);

bool remote_index_isnew(const struct remote_index *, struct donefile *);
void remote_index_needed(struct remote_index *);
void remote_index_markdone(const struct remote_index *, struct markdonefile *);

char *genlistsfilename(/*@null@*/const char * /*type*/, unsigned int /*count*/, ...) __attribute__((sentinel));

struct cachedlistfile;
retvalue cachedlists_scandir(/*@out@*/struct cachedlistfile **);
void cachedlistfile_need_index(struct cachedlistfile *, const char * /*repository*/, const char * /*suite*/, const char * /*architecture*/, const char * /*component*/, packagetype_t);
void cachedlistfile_need_flat_index(struct cachedlistfile *, const char * /*repository*/, const char * /*suite*/, packagetype_t);
void cachedlistfile_need(struct cachedlistfile *, const char * /*type*/, unsigned int /*count*/, ...) __attribute__((sentinel));
void cachedlistfile_freelist(/*@only@*/struct cachedlistfile *);
void cachedlistfile_deleteunneeded(const struct cachedlistfile *);
#endif
