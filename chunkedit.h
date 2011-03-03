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
	enum cefaction { CEF_DELETE,    /* delete if there */
		       	CEF_ADDMISSED, /* add if not there */
		       	CEF_REPLACE,   /* replace if there */
		       	CEF_ADD,      /* add if not there or replace if there */
			CEF_KEEP   /* keep it */
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
/*@null@*/struct chunkeditfield *cef_newfield(const char *field, enum cefaction action, enum cefwhen when, unsigned int linecount, /*@only@*//*@null@*/struct chunkeditfield *next);

void cef_setdata(struct chunkeditfield *cef, const char *data);
void cef_setdatalen(struct chunkeditfield *cef, const char *data, size_t len);
/* calculate the length, do not change the strlist after that before free */
void cef_setwordlist(struct chunkeditfield *cef, const struct strlist *words);
retvalue cef_setline(struct chunkeditfield *cef, int line, int wordcount, ...);

retvalue chunk_edit(const char *chunk, char **result, size_t *len, const struct chunkeditfield *cef);

void cef_free(/*@only@*//*@null@*/struct chunkeditfield *f);

#endif
