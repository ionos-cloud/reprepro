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

void strlist_init(/*@out@*/struct strlist *strlist);
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

/* print a space separated list of elements */
retvalue strlist_fprint(FILE *file,const struct strlist *strlist);

/* duplicate with content */
retvalue strlist_dup(struct strlist *dest,const struct strlist *orig);
/* replace the contents of dest with those from orig, which get emptied */
void strlist_move(/*@out@*/struct strlist *dest,struct strlist *orig);
/* empty orig and add everything to the end of dest, on error nothing is freed */
retvalue strlist_mvadd(struct strlist *dest,struct strlist *orig);

bool strlist_in(const struct strlist *strlist, const char *element);
int strlist_ofs(const struct strlist *strlist,const char *element);

bool strlist_intersects(const struct strlist *, const struct strlist *);
/* if missing != NULL And subset no subset of strlist, set *missing to the first missing one */
bool strlist_subset(const struct strlist *strlist, const struct strlist *subset, const char **missing);

/* a list of bool for all values, duplicates already set to true */
bool *strlist_preparefoundlist(const struct strlist *, bool ignorenone);

/* concatenate <prefix> <values separated by infix> <suffix> */
char *strlist_concat(const struct strlist *, const char *prefix, const char *infix, const char *suffix);

/* remove all strings equal to the argument */
void strlist_remove(struct strlist *, const char *);
#endif
