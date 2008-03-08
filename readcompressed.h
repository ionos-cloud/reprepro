#ifndef REPREPRO_READCOMPRESSED_H
#define REPREPRO_READCOMPRESSED_H

// TODO: this might be moved to some common code for also parsing index files
// later...


enum compression { c_uncompressed, c_gzipped, c_bzipped, c_lzmad };

#ifdef HAVE_LIBBZ2
#define unsupportedcompression(x) ( x > c_bzipped )
#else
#define unsupportedcompression(x) ( x >= c_bzipped )
#endif

struct readcompressed;

bool readcompressed_getline(struct readcompressed *, /*@out@*/const char **);
char readcompressed_overlinegetchar(struct readcompressed *);
retvalue readcompressed_open(/*@out@*/struct readcompressed **, const char *, enum compression);
retvalue readcompressed_close(/*@only@*/struct readcompressed *);
void readcompressed_abort(/*@only@*/struct readcompressed *);

#endif

