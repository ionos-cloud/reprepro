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

/* look for name in chunk. returns RET_NOTHING if not found */
retvalue chunk_getvalue(const char *chunk,const char *name,char **value);
retvalue chunk_getfirstword(const char *chunk,const char *name,char **value);
retvalue chunk_getextralinelist(const char *chunk,const char *name,struct strlist *strlist);
retvalue chunk_getwordlist(const char *chunk,const char *name,struct strlist *strlist);


typedef retvalue chunkaction(void *data,const char *chunk);

/* Call action for each chunk in <filename> */
retvalue chunk_foreach(const char *filename,chunkaction action,void *data,int force);

#endif
