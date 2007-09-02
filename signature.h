#ifndef REPREPRO_SIGNATURE_H
#define REPREPRO_SIGNATURE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* does not need to be called if allowpassphrase if false,
 * argument will only take effect if called the first time */
retvalue signature_init(bool allowpassphrase);

retvalue signature_check(const char *options, const char *releasegpg, const char *release);
retvalue signature_sign(const char *options, const char *filename, const char *signeturename);

/* Read a single chunk from a file, that may be signed. */
retvalue signature_readsignedchunk(const char *filename, const char *filenametoshow, char **chunkread, /*@null@*/ /*@out@*/struct strlist *validkeys, /*@null@*/ /*@out@*/ struct strlist *allkeys, bool *brokensignature);

struct signedfile;

retvalue signature_startsignedfile(const char *directory, const char *basename, /*@out@*/const char *options, struct signedfile **);
retvalue signature_startunsignedfile(const char *directory, const char *basename, /*@out@*/struct signedfile **);
void signedfile_write(struct signedfile *, const void *, size_t);
/* generate signature in temporary file */
retvalue signedfile_prepare(struct signedfile *, const char *options);
/* move temporary files to final places */
retvalue signedfile_finalize(struct signedfile *, bool *toolate);
/* may only be called after signedfile_prepare */
retvalue signedfile_free(struct signedfile *);

void signatures_done(void);

#endif
