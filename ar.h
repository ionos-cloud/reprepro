#ifndef DEBCOMP_AR_H
#define DEBCOMP_AR_H

struct ar_archive;

retvalue ar_open(/*@out@*/struct ar_archive **, const char *);
void ar_close(/*@only@*/struct ar_archive *);

/* RET_OK = next is there, RET_NOTHING = eof, < 0 = error */
retvalue ar_nextmember(struct ar_archive *, /*@out@*/char ** /*filename*/);

/* set compression for the next member */
void ar_archivemember_setcompression(struct ar_archive *, enum compression);

/* the following can be used for libarchive to read an file in the ar
 * after ar_nextmember returned successfully.
 * All references get invalid after the ar_nextmember is called again.  */
int ar_archivemember_close(struct archive *, void *);
int ar_archivemember_open(struct archive *, void *);
ssize_t ar_archivemember_read(struct archive *, void *, const void **);

#endif
