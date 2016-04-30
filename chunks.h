#ifndef REPREPRO_CHUNKS_H
#define REPREPRO_CHUNKS_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

/* look for name in chunk. returns RET_NOTHING if not found */
retvalue chunk_getvalue(const char *, const char *, /*@out@*/char **);
retvalue chunk_getextralinelist(const char *, const char *, /*@out@*/struct strlist *);
retvalue chunk_getwordlist(const char *, const char *, /*@out@*/struct strlist *);
retvalue chunk_getuniqwordlist(const char *, const char *, /*@out@*/struct strlist *);
retvalue chunk_getwholedata(const char *, const char *, /*@out@*/char **value);

/* Parse a package/source-field: ' *value( ?\(version\))? *' */
retvalue chunk_getname(const char *, const char *, /*@out@*/char **, bool /*allowversion*/);
retvalue chunk_getnameandversion(const char *, const char *, /*@out@*/char **, /*@out@*/char **);

/* return RET_OK, if field is found, RET_NOTHING, if not (or value indicates false) */
retvalue chunk_gettruth(const char *, const char *);
/* return RET_OK, if field is found, RET_NOTHING, if not */
retvalue chunk_checkfield(const char *, const char *);

/* modifications of a chunk: */
struct fieldtoadd {
	/*@null@*/struct fieldtoadd *next;
	/* The name of the field: */
	/*@dependent@*/const char *field;
	/* The data to include: (if NULL, delete this field) */
	/*@null@*//*@dependent@*/const char *data;
	/* how many chars in them (the *exact* len to use
	 *                        , no \0 allowed within!), */
	size_t len_field, len_data;
};

// TODO make this return retvalue..
/* Add this the <fields to add> to <chunk> before <beforethis> field,
 * replacing older fields of this name, if they are already there. */
/*@null@*/ char *chunk_replacefields(const char *, const struct fieldtoadd *, const char * /*beforethis*/, bool /*maybemissing*/);
/*@null@*/struct fieldtoadd *deletefield_new(/*@dependent@*/const char *, /*@only@*//*@null@*/struct fieldtoadd *);
/*@null@*/struct fieldtoadd *aodfield_new(/*@dependent@*/const char *, /*@dependent@*//*@null@*/const char *, /*@only@*/struct fieldtoadd *);
/*@null@*/struct fieldtoadd *addfield_new(/*@dependent@*/const char *, /*@dependent@*//*@null@*/const char *, /*@only@*/struct fieldtoadd *);
/*@null@*/struct fieldtoadd *addfield_newn(/*@dependent@*/const char *, /*@dependent@*//*@null@*/const char *, size_t, /*@only@*/struct fieldtoadd *);
void addfield_free(/*@only@*//*@null@*/struct fieldtoadd *);

/* that is chunk_replacefields(chunk,{fieldname,strlen,data,strlen},fieldname); */
/*@null@*/char *chunk_replacefield(const char *, const char *, const char *, bool /*maybemissing*/);

/* make sure a given field is first and remove any later occurrences */
/*@null@*/char *chunk_normalize(const char *, const char *, const char *);

/* reformat control data, removing leading spaces and CRs */
size_t chunk_extract(char * /*buffer*/, const char */*start*/, size_t, bool, /*@out@*/const char ** /*next*/);
const char *chunk_getstart(const char *, size_t, bool /*commentsallowed*/);
const char *chunk_over(const char *);

#endif
