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

retvalue strlist_init(struct strlist *strlist);
retvalue strlist_init_n(int startsize,struct strlist *strlist);
void strlist_done(struct strlist *strlist);

/* add a string, will get property of the strlist and free'd by it */
retvalue strlist_add(struct strlist *strlist,char *element);

/* print a space seperated list of elements */
retvalue strlist_fprint(FILE *file,const struct strlist *strlist);

/* duplicate with content */
retvalue strlist_dup(struct strlist *dest,const struct strlist *orig);

int strlist_in(const struct strlist *strlist,const char *element);

#endif
