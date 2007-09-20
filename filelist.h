#ifndef REPREPRO_FILELIST_H
#define REPREPRO_FILELIST_H

#ifndef REPREPRO_RELEASE_H
#include "release.h"
#endif

struct filelist_package;
struct filelist_list;

retvalue filelist_init(struct filelist_list **list);
retvalue filelist_newpackage(struct filelist_list *filelist, const char *name, const char *section, const struct filelist_package **pkg);

retvalue filelist_add(struct filelist_list *,const struct filelist_package *,const char *);

retvalue filelist_write(struct filelist_list *list, struct filetorelease *file);

void filelist_free(/*@only@*/struct filelist_list *);

/* for use in routines reading the data: */
struct filelistcompressor {
	unsigned int offsets[256];
	size_t size, len, dirdept;
	char *filelist;
};
retvalue filelistcompressor_setup(/*@out@*/struct filelistcompressor *);
retvalue filelistcompressor_add(struct filelistcompressor *, const char *, size_t);
retvalue filelistcompressor_finish(struct filelistcompressor *, /*@out@*/char **);
void filelistcompressor_cancel(struct filelistcompressor *);
#endif
