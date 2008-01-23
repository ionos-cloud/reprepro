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
			ic_count /* fake item to get count */
};
typedef unsigned int compressionset; /* 1 << indexcompression */
#define IC_FLAG(a) (1<<(a))

/* Initialize Release generation */
retvalue release_init(struct release **release, struct database *database, const char *distdir, const char *codename);
/* same but for a snapshot */
retvalue release_initsnapshot(const char *distdir, const char *codename, const char *name, struct release **release);

retvalue release_mkdir(struct release *release, const char *relativedirectory);

const char *release_dirofdist(struct release *release);

retvalue release_addnew(struct release *release,/*@only@*/char *reltmpfile,/*@only@*/char *relfilename);
retvalue release_adddel(struct release *release,/*@only@*/char *reltmpfile);
retvalue release_addold(struct release *release,/*@only@*/char *relfilename);

struct filetorelease;

retvalue release_startfile2(struct release *release, const char *relative_dir, const char *filename, compressionset compressions, bool usecache, struct filetorelease **file);

retvalue release_startfile(struct release *release, const char *filename, compressionset compressions, bool usecache, struct filetorelease **file);

/* return true if an old file is already there */
bool release_oldexists(struct filetorelease *file);

/* errors will be cached for release_finishfile */
retvalue release_writedata(struct filetorelease *file, const char *data, size_t len);
#define release_writestring(file,data) release_writedata(file,data,strlen(data))

void release_abortfile(/*@only@*/struct filetorelease *file);
retvalue release_finishfile(struct release *release, /*@only@*/struct filetorelease *file);

struct distribution;
struct target;
retvalue release_directorydescription(struct release *release, const struct distribution *distribution, const struct target *target, const char *filename, bool onlyifneeded);

void release_free(/*@only@*/struct release *release);
retvalue release_prepare(struct release *release, struct distribution *distribution, bool onlyneeded);
retvalue release_finish(/*@only@*/struct release *release, struct distribution *distribution);

#endif
