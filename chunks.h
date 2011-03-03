#ifndef REPREPRO_CHUNKS_H
#define REPREPRO_CHUNKS_H

#include <zlib.h>

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

/* get the next chunk from file f ( return RET_NOTHING, if there are none )*/
retvalue chunk_read(gzFile f,/*@out@*/char **chunk);

/* look for name in chunk. returns RET_NOTHING if not found */
retvalue chunk_getvalue(const char *chunk,const char *name,/*@out@*/char **value);
retvalue chunk_getfirstword(const char *chunk,const char *name,/*@out@*/char **value);
retvalue chunk_getextralinelist(const char *chunk,const char *name,/*@out@*/struct strlist *strlist);
retvalue chunk_getwordlist(const char *chunk,const char *name,/*@out@*/struct strlist *strlist);
retvalue chunk_getuniqwordlist(const char *chunk,const char *name,/*@out@*/struct strlist *strlist);
retvalue chunk_getwholedata(const char *chunk,const char *name,/*@out@*/char **value);

/* Parse a package/source-field: ' *value( ?\(version\))? *' */
retvalue chunk_getname(const char *chunk, const char *name, /*@out@*/char **pkgname, bool allowversion);
retvalue chunk_getnameandversion(const char *chunk,const char *name,/*@out@*/char **pkgname,/*@out@*/char **version);

/* return RET_OK, if field is found, RET_NOTHING, if not (or value indicates false in future variants) */
retvalue chunk_gettruth(const char *chunk,const char *name);
/* return RET_OK, if field is found, RET_NOTHING, if not */
retvalue chunk_checkfield(const char *chunk,const char *name);


typedef retvalue chunkaction(/*@temp@*/void *data,/*@temp@*/const char *chunk);

/* Call action for each chunk in <filename>,
 * until error or until ok when <stopwhenok> */
retvalue chunk_foreach(const char *filename, chunkaction action, /*@null@*/ /*@temp@*/void *data, bool stopwhenok);

/* modifications of a chunk: */
struct fieldtoadd {
	/*@null@*/struct fieldtoadd *next;
	/* The name of the field: */
	/*@dependent@*/const char *field;
	/* The data to include: (if NULL, delete this field) */
	/*@null@*//*@dependent@*/const char *data;
	/* how many chars in them (the *exact* len to use
	 *                        , no \0 allowed within!), */
	size_t len_field,len_data;
};

// TODO make this return retvalue..
/* Add this the <fields to add> to <chunk> before <beforethis> field,
 * replacing older fields of this name, if they are already there. */
/*@null@*/ char *chunk_replacefields(const char *chunk,const struct fieldtoadd *toadd,const char *beforethis);
/*@null@*/struct fieldtoadd *deletefield_new(/*@dependent@*/const char *field,/*@only@*//*@null@*/struct fieldtoadd *next);
/*@null@*/struct fieldtoadd *addfield_new(/*@dependent@*/const char *field,/*@dependent@*//*@null@*/const char *data,/*@only@*/struct fieldtoadd *next);
/*@null@*/struct fieldtoadd *addfield_newn(/*@dependent@*/const char *field,/*@dependent@*//*@null@*/const char *data,size_t len,/*@only@*/struct fieldtoadd *next);
void addfield_free(/*@only@*//*@null@*/struct fieldtoadd *f);

/* that is chunk_replacefields(chunk,{fieldname,strlen,data,strlen},fieldname); */
/*@null@*/char *chunk_replacefield(const char *chunk,const char *fieldname,const char *data);

/* check if all field names  are in allowedfieldnames */
retvalue chunk_checkfields(const char *chunk, const char * const *allowedfieldnames, bool commentsallowed);

#endif
