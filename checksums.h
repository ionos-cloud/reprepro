#ifndef REPREPRO_CHECKSUMS_H
#define REPREPRO_CHECKSUMS_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

enum checksumtype { cs_md5sum, cs_sha1sum, cs_count };

struct checksums;

void checksums_free(/*@only@*//*@null@*/struct checksums *);

/* duplicate a checksum record, NULL means OOM */
struct checksums *checksums_dup(const struct checksums *);

/* create a checksum record from an md5sum: */
retvalue checksums_set(/*@out@*/struct checksums **, /*@only@*/char *);
retvalue checksums_setall(/*@out@*/struct checksums **checksums_p, const char *combinedchecksum, size_t len, /*@only@*//*@null@*/ char *md5sum);

/* hashes[*] is free'd */
retvalue checksums_init(/*@out@*/struct checksums **, char *hashes[cs_count+1]);
retvalue checksums_parse(/*@out@*/struct checksums **, const char *);

retvalue checksums_get(const struct checksums *, enum checksumtype, /*@out@*/char **);
off_t checksums_getfilesize(const struct checksums *);

/* get 0-terminated combined textual representation of the checksums,
 * including the size (including the trailing '\0'): */
retvalue checksums_getcombined(const struct checksums *, /*@out@*/const char **, /*@out@*/size_t *);

/* get a static pointer to a specific part of a checksum (wihtout size) */
bool checksums_getpart(const struct checksums *, enum checksumtype, /*@out@*/const char **, /*@out@*/size_t *);
/* extract a single checksum from the combined data: */
bool checksums_gethashpart(const struct checksums *, enum checksumtype, /*@out@*/const char **hash_p, /*@out@*/size_t *hashlen_p, /*@out@*/const char **size_p, /*@out@*/size_t *sizelen_p);

/* check if a single checksum fits */
bool checksums_matches(const struct checksums *,enum checksumtype, const char *);

/* Copy file <origin> to file <destination>, calculating checksums */
retvalue checksums_copyfile(const char *destination, const char *origin, /*@out@*/struct checksums **);
retvalue checksums_hardlink(const char *directory, const char *filekey, const char *sourcefilename, const struct checksums *);

retvalue checksums_linkorcopyfile(const char *destination, const char *origin, /*@out@*/struct checksums **);

/* calculare checksums of a file: */
retvalue checksums_read(const char *fullfilename, /*@out@*/struct checksums **);

/* check if the file has the given md5sum (only cheap tests like size),
 * RET_NOTHING means file does not exist, RET_ERROR_WRONG_MD5 means wrong size */
retvalue checksums_cheaptest(const char *fullfilename, const struct checksums *, bool);

/* check if checksum of filekey in database and checksum of actual file, set improve if some new has is in the last */
bool checksums_check(const struct checksums *, const struct checksums *, /*@out@*/bool *improves);

/* return true, iff all supported checksums are available */
bool checksums_iscomplete(const struct checksums *);

/* Collect missing checksums (if all are there always RET_OK without checking).
 * if the file is not there, return RET_NOTHING,
 * if it is but not matches, return RET_ERROR_WRONG_MD5 */
retvalue checksums_complete(struct checksums **, const char *fullfilename);

void checksums_printdifferences(FILE *,const struct checksums *expected, const struct checksums *got);

retvalue checksums_combine(struct checksums **checksums, const struct checksums *by);

struct checksumsarray {
	struct strlist names;
	/*@null@*/struct checksums **checksums;
};
void checksumsarray_move(/*@out@*/struct checksumsarray *, struct checksumsarray *);
void checksumsarray_done(struct checksumsarray *);
retvalue checksumsarray_parse(/*@out@*/struct checksumsarray *, const struct strlist *, const char *filenametoshow);
retvalue checksumsarray_include(struct checksumsarray *, /*@only@*/char *, const struct checksums *);


#ifdef CHECKSUMS_CONTEXT
#ifndef MD5_H
#include "md5.h"
#endif
#ifndef REPREPRO_SHA1_H
#include "sha1.h"
#endif

struct checksumscontext {
	struct MD5Context md5;
	struct SHA1_Context sha1;
};

void checksumscontext_init(/*@out@*/struct checksumscontext *);
void checksumscontext_update(struct checksumscontext *, const unsigned char *, size_t);
retvalue checksums_from_context(/*@out@*/struct checksums **, struct checksumscontext *);
#endif

#endif
