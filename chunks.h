#ifndef __MIRRORER_CHUNKS_H
#define __MIRRORER_CHUNKS_H

#include <zlib.h>

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#include "strlist.h"

/* get the next chunk from file f */
char *chunk_read(gzFile f);

/* look for name in chunk. returns RET_NOTHING if not found */
retvalue chunk_getvalue(const char *chunk,const char *name,char **value);
retvalue chunk_getfirstword(const char *chunk,const char *name,char **value);
retvalue chunk_getextralinelist(const char *chunk,const char *name,struct strlist *strlist);
retvalue chunk_getwordlist(const char *chunk,const char *name,struct strlist *strlist);
/* return RET_OK, if field is found, RET_NOTHING, if not (or value indicates false in future variants) */ 
retvalue chunk_gettruth(const char *chunk,const char *name);
/* return RET_OK, if field is found, RET_NOTHING, if not */ 
retvalue chunk_checkfield(const char *chunk,const char *name);


typedef retvalue chunkaction(void *data,const char *chunk);

/* Call action for each chunk in <filename>, 
 * until error when not <force> or until ok when <stopwhenok> */
retvalue chunk_foreach(const char *filename,chunkaction action,void *data,int force,int stopwhenok);

/* modifications of a chunk: */
struct fieldtoadd {
	struct fieldtoadd *next;
	/* The name of the field: */
	const char *field;
	/* The data to include: (if NULL, delete this field) */
	const char *data;
	/* how many chars in them (the *exact* len to use
	 *                        , no \0 allowed within!), */
	size_t len_field,len_data;
};

// TODO make this return retvalue..
/* Add this the <fields to add> to <chunk> before <beforethis> field,
 * replacing older fields of this name, if they are already there. */
char *chunk_replacefields(const char *chunk,const struct fieldtoadd *toadd,const char *beforethis);
struct fieldtoadd *deletefield_new(const char *field,struct fieldtoadd *next);
struct fieldtoadd *addfield_new(const char *field,const char *data,struct fieldtoadd *next);
struct fieldtoadd *addfield_newn(const char *field,const char *data,size_t len,struct fieldtoadd *next);
void addfield_free(struct fieldtoadd *f);

/* that is chunk_replacefields(chunk,{fieldname,strlen,data,strlen},fieldname); */
char *chunk_replacefield(const char *chunk,const char *fieldname,const char *data);

#endif
