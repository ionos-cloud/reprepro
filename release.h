#ifndef REPREPRO_RELEASE_H
#define REPREPRO_RELEASE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

struct release;

#define ic_first ic_uncompressed
enum indexcompression {ic_uncompressed=0, ic_gzip,
#ifdef HAVE_LIBBZ2
			ic_bzip2,
#endif
#ifdef HAVE_LIBLZMA
			ic_xz,
#endif
			ic_count /* fake item to get count */
};
typedef unsigned int compressionset; /* 1 << indexcompression */
#define IC_FLAG(a) (1<<(a))

/* Initialize Release generation */
retvalue release_init(struct release **, const char * /*codename*/, /*@null@*/const char * /*suite*/, /*@null@*/const char * /*fakeprefix*/);
/* same but for a snapshot */
retvalue release_initsnapshot(const char *codename, const char *name, struct release **);

retvalue release_mkdir(struct release *, const char * /*relativedirectory*/);

const char *release_dirofdist(struct release *);

retvalue release_addnew(struct release *, /*@only@*/char *, /*@only@*/char *);
retvalue release_addsilentnew(struct release *, /*@only@*/char *, /*@only@*/char *);
retvalue release_adddel(struct release *, /*@only@*/char *);
retvalue release_addold(struct release *, /*@only@*/char *);

struct filetorelease;

retvalue release_startfile(struct release *, const char * /*filename*/, compressionset, bool /*usecache*/, struct filetorelease **);
retvalue release_startlinkedfile(struct release *, const char * /*filename*/, const char * /*symlinkas*/, compressionset, bool /*usecache*/, struct filetorelease **);
void release_warnoldfileorlink(struct release *, const char *, compressionset);

/* return true if an old file is already there */
bool release_oldexists(struct filetorelease *);

/* errors will be cached for release_finishfile */
retvalue release_writedata(struct filetorelease *, const char *, size_t);
#define release_writestring(file, data) release_writedata(file, data, strlen(data))

void release_abortfile(/*@only@*/struct filetorelease *);
retvalue release_finishfile(struct release *, /*@only@*/struct filetorelease *);

struct distribution;
struct target;
retvalue release_directorydescription(struct release *, const struct distribution *, const struct target *, const char * /*filename*/, bool /*onlyifneeded*/);

void release_free(/*@only@*/struct release *);
retvalue release_prepare(struct release *, struct distribution *, bool /*onlyneeded*/);
retvalue release_finish(/*@only@*/struct release *, struct distribution *);

#endif
