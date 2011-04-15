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
	int count, size;
};

void strlist_init(/*@out@*/struct strlist *);
retvalue strlist_init_n(int /*startsize*/, /*@out@*/struct strlist *);
retvalue strlist_init_singleton(/*@only@*/char *, /*@out@*/struct strlist *);
void strlist_done(/*@special@*/struct strlist *strlist) /*@releases strlist->values @*/;

/* add a string, will get property of the strlist and free'd by it */
retvalue strlist_add(struct strlist *, /*@only@*/char *);
/* include a string at the beginning, otherwise like strlist_add */
retvalue strlist_include(struct strlist *, /*@only@*/char *);
/* add a string alphabetically, discarding if already there. */
retvalue strlist_adduniq(struct strlist *, /*@only@*/char *);
/* like strlist_add, but strdup it first */
retvalue strlist_add_dup(struct strlist *strlist, const char *todup);

/* print a space separated list of elements */
retvalue strlist_fprint(FILE *, const struct strlist *);

/* replace the contents of dest with those from orig, which get emptied */
void strlist_move(/*@out@*/struct strlist *dest, /*@special@*/struct strlist *orig) /*@releases orig->values @*/;

bool strlist_in(const struct strlist *, const char *);
int strlist_ofs(const struct strlist *, const char *);

bool strlist_intersects(const struct strlist *, const struct strlist *);
/* if missing != NULL And subset no subset of strlist, set *missing to the first missing one */
bool strlist_subset(const struct strlist *, const struct strlist * /*subset*/, const char ** /*missing_p*/);

/* concatenate <prefix> <values separated by infix> <suffix> */
char *strlist_concat(const struct strlist *, const char * /*prefix*/, const char * /*infix*/, const char * /*suffix*/);

/* remove all strings equal to the argument */
void strlist_remove(struct strlist *, const char *);
#endif
