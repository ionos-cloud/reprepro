#ifndef REPREPRO_READCOMPRESSED_H
#define REPREPRO_READCOMPRESSED_H

// TODO: integrate this into uncompress.c...

#define unsupportedcompression(x) ( x >= c_bzip2 )

struct readcompressed;

bool readcompressed_getline(struct readcompressed *, /*@out@*/const char **);
char readcompressed_overlinegetchar(struct readcompressed *);
retvalue readcompressed_open(/*@out@*/struct readcompressed **, const char *, enum compression);
retvalue readcompressed_close(/*@only@*/struct readcompressed *);
void readcompressed_abort(/*@only@*/struct readcompressed *);

#endif

