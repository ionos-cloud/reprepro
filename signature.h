#ifndef REPREPRO_SIGNATURE_H
#define REPREPRO_SIGNATURE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* does not need to be called if allowpassphrase if false,
 * argument will only take effect if called the first time */
retvalue signature_init(bool allowpassphrase);

struct signature_requirement;
void signature_requirements_free(/*@only@*/struct signature_requirement *);
retvalue signature_requirement_add(struct signature_requirement **, const char *);
void free_known_keys(void);

retvalue signature_check(const struct signature_requirement *, const char *, const char *, const char *, size_t);
retvalue signature_check_inline(const struct signature_requirement *, const char *, /*@out@*/char **);


struct signatures {
	int count, validcount;
	struct signature {
		char *keyid;
		char *primary_keyid;
		/* valid is only true if none of the others is true,
		   all may be false due to non-signing keys used for
		   signing or things like that */
		enum signature_state {
			/* internal error: */
			sist_error=0,
			/* key missing, can not be checked: */
			sist_missing,
			/* broken signature, content may be corrupt: */
			sist_bad,
			/* good signature, but may not sign or al: */
			sist_invalid,
			/* good signature, but check expire bits: */
			sist_mostly,
			/* good signature, no objections: */
			sist_valid
		} state;
		/* subkey or primary key are expired */
		bool expired_key;
		/* signature is expired */
		bool expired_signature;
		/* key or primary key revoced */
		bool revoced_key;
	} signatures[];
};
void signatures_free(/*@null@*//*@only@*/struct signatures *);
/* Read a single chunk from a file, that may be signed. */
retvalue signature_readsignedchunk(const char *filename, const char *filenametoshow, char **chunkread, /*@null@*/ /*@out@*/struct signatures **signatures, bool *brokensignature);

struct signedfile;
struct strlist;

retvalue signature_startsignedfile(/*@out@*/struct signedfile **);
void signedfile_write(struct signedfile *, const void *, size_t);
/* generate signature in temporary file */
retvalue signedfile_create(struct signedfile *, const char *, char **, char **, const struct strlist *, bool /*willcleanup*/);
void signedfile_free(/*@only@*/struct signedfile *);

void signatures_done(void);
#endif
