#ifndef __MIRRORER_STRLIST_H
#define __MIRRORER_STRLIST_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

struct strlist {
	char **values;
	int count,size;
};

retvalue strlist_new(struct strlist *strlist);
void strlist_free(struct strlist *strlist);

/* add a string, will get property of the strlist and free'd by it */
retvalue strlist_add(struct strlist *strlist,char *element);

int strlist_in(const struct strlist *strlist,const char *element);

#endif
