#ifndef REPREPRO_CHUNKEDIT_H
#define REPREPRO_CHUNKEDIT_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif


/* modifications of a chunk: */
struct chunkeditfield {
	/*@null@*/struct chunkeditfield *next;
	/* The name of the field: */
	const char *field; size_t len_field;
	enum cefaction { CEF_DELETE,   /* delete if there */
		       	CEF_ADDMISSED, /* add if not there */
		       	CEF_REPLACE,   /* replace if there */
		       	CEF_ADD,       /* add if not there or replace if there */
			CEF_KEEP       /* keep it */
	} action;
	enum cefwhen { CEF_EARLY, CEF_LATE } when;
	/* the following must be 0 or NULL for CEF_DELETE */
	size_t len_all_data;
	/*@null@*/const char *data; size_t len_data;
	const struct strlist *words;
	int linecount;
	struct cef_line {
		int wordcount;
		const char **words;
		size_t *wordlen;
	} lines[];
};

/* those return NULL on out of memory and free next in that case */
/*@null@*/struct chunkeditfield *cef_newfield(const char *, enum cefaction, enum cefwhen, unsigned int /*linecount*/, /*@only@*//*@null@*/struct chunkeditfield *);

void cef_setdata(struct chunkeditfield *, const char *);
void cef_setdatalen(struct chunkeditfield *, const char *, size_t);
/* calculate the length, do not change the strlist after that before free */
void cef_setwordlist(struct chunkeditfield *, const struct strlist *);
retvalue cef_setline(struct chunkeditfield *, int /*line*/, int /*wordcount*/, ...);
retvalue cef_setline2(struct chunkeditfield *, int, const char *, size_t, const char *, size_t, int, ...);

retvalue chunk_edit(const char *, char **, size_t *, const struct chunkeditfield *);

void cef_free(/*@only@*//*@null@*/struct chunkeditfield *);

static inline struct chunkeditfield *cef_pop(/*@only@*/struct chunkeditfield *cef) {
	struct chunkeditfield *next = cef->next;
	cef->next = NULL;
	cef_free(cef);
	return next;
}

#endif
