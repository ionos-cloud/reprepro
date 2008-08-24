#ifndef REPREPRO_UNCOMPRESS_H
#define REPREPRO_UNCOMPRESS_H

/* "", ".gz", ... */
extern const char * const uncompression_suffix[c_COUNT];
extern /*@null@*/ char *extern_uncompressors[c_COUNT];

/* there are two different modes: uncompress a file to memory,
 * or uncompress (possibly multiple files) on the filesystem,
 * controled by aptmethods */

#ifdef HAVE_LIBBZ2
#define ONLYWITHBZ2(a) a
#else
#define ONLYWITHBZ2(a)
#endif
#define uncompression_supported(c) ( \
		ONLYWITHBZ2( (c) == c_bzip2 || ) \
		(c) == c_gzip || \
		(c) == c_none || \
		extern_uncompressors[c] != NULL)

/**** functions for aptmethod.c ****/

/* we got an pid, check if it is a uncompressor we care for */
retvalue uncompress_checkpid(pid_t, int);
/* still waiting for a client to exit */
bool uncompress_running(void);

typedef retvalue finishaction(void *, const char *, bool);
/* uncompress and call action when finished */
retvalue uncompress_queue_file(const char *, const char *, enum compression, finishaction *, void *, bool *);

/**** functions for update.c (uncompressing an earlier downloaded file) ****/

retvalue uncompress_file(const char *, const char *, enum compression);

/**** functions for indexfile.c (uncompressing to memory) ****/
// and perhaps also sourceextraction.c

struct compressedfile;

retvalue uncompress_open(/*@out@*/struct compressedfile **, const char *, enum compression);
int uncompress_read(struct compressedfile *, void *buffer, int);
retvalue uncompress_close(/*@only@*/struct compressedfile *);

/**** general initialisation ****/

/* check for existance of external programs */
void uncompressions_check(void);

#endif

