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
/* point to a specified field in a chunk */
// const char *chunk_getfield(const char *name,const char *chunk);
/* create a new chunk with the context of field name replaced with new,
 * prints an error when not found and adds to the end */
char *chunk_replaceentry(const char *chunk,const char *name,const char *new);
/* create a new chunk with the given data added before another field */
char *chunk_insertdata(const char *chunk,const char *before,const char *new);

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

/* Call action for each chunk in <filename> */
retvalue chunk_foreach(const char *filename,chunkaction action,void *data,int force);

/* modifications of a chunk: */
struct fieldtoadd {
	struct fieldtoadd *next;
	/* The name of the field: */
	const char *field;
	/* The data to include: */
	const char *data;
	/* how many chars in them (the *exact* len to use
	 *                        , no \0 allowed within!), */
	size_t len_field,len_data;
};

/* Add this the <fields to add> to <chunk> before <beforethis> field,
 * replacing older fields of this name, if they are already there. */
retvalue chunk_replacefields(char **chunk,const struct fieldtoadd *toadd,const char *beforethis);
struct fieldtoadd *addfield_new(const char *field,const char *data,struct fieldtoadd *next);
struct fieldtoadd *addfield_newn(const char *field,const char *data,size_t len,struct fieldtoadd *next);
void addfield_free(struct fieldtoadd *f);

#endif
