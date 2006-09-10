#ifndef REPREPRO_STRLIST_H
#define REPREPRO_STRLIST_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_GLOBALS_H
#include "globals.h"
#warning "What's hapening here?"
#endif

struct strlist {
	char **values;
	int count,size;
};

retvalue strlist_init(/*@out@*/struct strlist *strlist);
retvalue strlist_init_n(int startsize,/*@out@*/struct strlist *strlist);
retvalue strlist_init_singleton(/*@only@*/char *value,/*@out@*/struct strlist *strlist);
void strlist_done(struct strlist *strlist);

/* add a string, will get property of the strlist and free'd by it */
retvalue strlist_add(struct strlist *strlist,/*@only@*/char *element);
/* include a string at the beginning, otherwise like strlist_add */
retvalue strlist_include(struct strlist *strlist,/*@only@*/char *element);
/* add a string alphabetically, discarding if already there. */
retvalue strlist_adduniq(struct strlist *strlist,/*@only@*/char *element);
/* like strlist_add, but strdup it first */
retvalue strlist_add_dup(struct strlist *strlist, const char *todup);

/* print a space seperated list of elements */
retvalue strlist_fprint(FILE *file,const struct strlist *strlist);

/* duplicate with content */
retvalue strlist_dup(struct strlist *dest,const struct strlist *orig);
/* replace the contents of dest with those from orig, which get emptied */
void strlist_move(/*@out@*/struct strlist *dest,struct strlist *orig);
/* empty orig and add everything to the end of dest, on error nothing is freed */
retvalue strlist_mvadd(struct strlist *dest,struct strlist *orig);

bool_t strlist_in(const struct strlist *strlist,const char *element);
int strlist_ofs(const struct strlist *strlist,const char *element);

/* if missing != NULL And subset no subset of strlist, set *missing to the first missing one */
bool_t strlist_subset(const struct strlist *strlist,const struct strlist *subset,const char **missing);

#endif
