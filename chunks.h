#ifndef __MIRRORER_CHUNKS_H
#define __MIRRORER_CHUNKS_H

#include <zlib.h>

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* get the next chunk from file f */
char *chunk_read(gzFile f);
/* point to a specified field in a chunk */
const char *chunk_getfield(const char *name,const char *chunk);
/* strdup a field given by chunk_getfield */
char *chunk_dupvalue(const char *field);
/* strdup the following lines of a field */
char *chunk_dupextralines(const char *field);
/* strdup the first word of a field */
char *chunk_dupword(const char *field);
/* create a new chunk with the context of field name replaced with new,
 * prints an error when not found and adds to the end */
char *chunk_replaceentry(const char *chunk,const char *name,const char *new);

/* worditerator */
struct worditerator { const char *c; };
retvalue chunk_worditerator_get(const struct worditerator *iterator,char **word);
retvalue chunk_worditerator_next(struct worditerator *iterator);

/* look for name in chunk. returns RET_NOTHING if not found */
retvalue chunk_getvalue(const char *chunk,const char *name,char **value);
/* get a word iterator for the given field */
retvalue chunk_getworditerator(const char *chunk,const char *name,struct worditerator *iterator);


typedef retvalue chunkaction(void *data,const char *chunk);

/* Call action for each chunk in <filename> */
retvalue chunk_foreach(const char *filename,chunkaction action,void *data,int force);

#endif
