#ifndef __MIRRORER_CHUNKS_H
#define __MIRRORER_CHUNKS_H

#include <zlib.h>

/* get the next chunk from file f */
char *chunk_read(gzFile f);
/* point to a specified field in a chunk */
const char *chunk_getfield(const char *name,const char *chunk);
/* strdup a field given by chunk_getfield */
char *chunk_dupvalue(const char *field);
/* strdup without a leading "<prefix>/" */
char *chunk_dupvaluecut(const char *field,const char *prefix);
/* strdup the following lines of a field */
char *chunk_dupextralines(const char *field);
/* strdup the first word of a field */
char *chunk_dupword(const char *field);
/* create a new chunk with the context of field name replaced with new,
 * prints an error when not found and adds to the end */
char *chunk_replaceentry(const char *chunk,const char *name,const char *new);

#endif
