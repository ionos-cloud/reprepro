#ifndef REPREPRO_CHECKSUMS_H
#define REPREPRO_CHECKSUMS_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

enum checksumtype { cs_md5sum, cs_sha1sum, cs_count };

struct checksums;

void checksums_free(/*@only@*//*@null@*/struct checksums *);

/* create a checksum record from an md5sum: */
retvalue checksums_set(/*@out@*/struct checksums **, /*@only@*/char *);

/* extract a single checksum from the combined data: */
retvalue checksums_get(const struct checksums *, enum checksumtype, /*@out@*/char **);

/* get a static pointer to a specific part of a checksum (wihtout size) */
retvalue checksums_getpart(const struct checksums *, enum checksumtype, /*@out@*/const char **, /*@out@*/size_t *);

/* check if a single checksum fits */
bool checksums_matches(const struct checksums *,enum checksumtype, const char *);

/* Copy file <origin> to file <destination>, calculating checksums */
retvalue checksums_copyfile(const char *destination, const char *origin, /*@out@*/struct checksums **);

/* calculare checksums of a file: */
retvalue checksums_read(const char *fullfilename, /*@out@*/struct checksums **);

/* check if checksum of filekey in database and checksum of actual file, set improve if some new has is in the last */
bool checksums_check(const struct checksums *, const struct checksums *, /*@out@*/bool *improves);

void checksums_printdifferences(FILE *,const struct checksums *expected, const struct checksums *got);

retvalue checksums_combine(struct checksums **checksums, /*@only@*/struct checksums *by);

/* stuff still in md5sums.c: */
retvalue checksum_read(const char *filename, /*@out@*/char **md5sum, /*@out@*/char **sha1sum);
retvalue checksum_complete(const char *directory, const char *filename, char *hashes[cs_count]);
retvalue checksum_combine(char **, const char *[cs_count]);
retvalue checksum_dismantle(const char *, char *[cs_count]);
#endif
