#ifndef REPREPRO_SIGNATURE_H
#define REPREPRO_SIGNATURE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* does not need to be called if allowpassphrase if FALSE,
 * argument will only take effect if called the first time */
retvalue signature_init(bool_t allowpassphrase);

retvalue signature_check(const char *options, const char *releasegpg, const char *release);
retvalue signature_sign(const char *options, const char *filename, const char *signeturename);

/* Read a single chunk from a file, that may be signed. */
retvalue signature_readsignedchunk(const char *filename, char **chunkread, bool_t onlyacceptsigned);

void signatures_done(void);

#endif
