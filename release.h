#ifndef REPREPRO_RELEASE_H
#define REPREPRO_RELEASE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

struct release;

enum indexcompression {ic_uncompressed=0, ic_gzip};
#define ic_count (ic_gzip+1)
typedef unsigned int compressionset; /* 1 << indexcompression */
#define IC_FLAG(a) (1<<(a))

/* Initialize Release generation */
retvalue release_init(const char *dbdir, const char *distdir, const char *codename, struct release **release);

const char *release_dirofdist(struct release *release);

retvalue release_addnew(struct release *release,/*@only@*/char *reltmpfile,/*@only@*/char *relfilename);
retvalue release_adddel(struct release *release,/*@only@*/char *reltmpfile);
retvalue release_addold(struct release *release,/*@only@*/char *relfilename);

struct filetorelease;

retvalue release_startfile2(struct release *release, const char *relative_dir, const char *filename, compressionset compressions, bool_t usecache, struct filetorelease **file);

retvalue release_startfile(struct release *release, const char *filename, compressionset compressions, bool_t usecache, struct filetorelease **file);

/* return TRUE if an old file is already there */
bool_t release_oldexists(struct filetorelease *file);

/* errors will be cached for release_finishfile */
retvalue release_writedata(struct filetorelease *file, const char *data, size_t len);
#define release_writestring(file,data) release_writedata(file,data,strlen(data))

void release_abortfile(/*@only@*/struct filetorelease *file);
retvalue release_finishfile(struct release *release, /*@only@*/struct filetorelease *file);

struct distribution;
struct target;
retvalue release_directorydescription(struct release *release, const struct distribution *distribution,const struct target *target,const char *filename,bool_t onlyifneeded);

void release_free(/*@only@*/struct release *release);
retvalue release_write(/*@only@*/struct release *release, struct distribution *distribution, bool_t onlyneeded);

#endif
