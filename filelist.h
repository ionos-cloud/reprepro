#ifndef REPREPRO_FILELIST_H
#define REPREPRO_FILELIST_H

#ifndef REPREPRO_RELEASE_H
#include "release.h"
#endif

struct filelist_list;
struct package;

retvalue filelist_init(struct filelist_list **list);

retvalue filelist_addpackage(struct filelist_list *, struct package *);

retvalue filelist_write(struct filelist_list *list, struct filetorelease *file);

void filelist_free(/*@only@*/struct filelist_list *);

retvalue fakefilelist(const char *filekey);
retvalue filelists_translate(struct table *, struct table *);

/* for use in routines reading the data: */
struct filelistcompressor {
	unsigned int offsets[256];
	size_t size, len;
	unsigned int dirdepth;
	char *filelist;
};
retvalue filelistcompressor_setup(/*@out@*/struct filelistcompressor *);
retvalue filelistcompressor_add(struct filelistcompressor *, const char *, size_t);
retvalue filelistcompressor_finish(struct filelistcompressor *, /*@out@*/char **, /*@out@*/size_t *);
void filelistcompressor_cancel(struct filelistcompressor *);
#endif
