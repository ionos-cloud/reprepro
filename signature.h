#ifndef REPREPRO_SIGNATURE_H
#define REPREPRO_SIGNATURE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

retvalue signature_check(const char *options, const char *releasegpg, const char *release);
retvalue signature_sign(const char *options, const char *filename);

/* Read a single chunk from a file, that may be signed. */
retvalue signature_readsignedchunk(const char *filename, char **chunkread, bool_t onlyacceptsigned);

#endif
