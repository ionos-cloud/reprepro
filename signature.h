#ifndef __MIRRORER_SIGNATURE_H
#define __MIRRORER_SIGNATURE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

retvalue signature_check(const char *chunk, const char *releasegpg, const char *release);
retvalue signature_sign(const char *chunk,const char *filename);

/* Read a single chunk from a file, that may be signed. */
retvalue signature_readsignedchunk(const char *filename, char **chunkread);

#endif
