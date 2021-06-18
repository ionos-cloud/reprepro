#ifndef REPREPRO_UNCOMPRESS_H
#define REPREPRO_UNCOMPRESS_H

/* "", ".gz", ... */
extern const char * const uncompression_suffix[c_COUNT];
extern /*@null@*/ char *extern_uncompressors[c_COUNT];
/* so help messages know which option to cite: */
extern const char * const uncompression_option[c_COUNT];
extern const char * const uncompression_config[c_COUNT];

/* there are two different modes: uncompress a file to memory,
 * or uncompress (possibly multiple files) on the filesystem,
 * controled by aptmethods */

#ifdef HAVE_LIBLZMA
# ifdef HAVE_LIBBZ2
#define uncompression_builtin(c) ((c) == c_xz || (c) == c_lzma || (c) == c_bzip2 || (c) == c_gzip)
# else
#define uncompression_builtin(c) ((c) == c_xz || (c) == c_lzma || (c) == c_gzip)
# endif
#else
# ifdef HAVE_LIBBZ2
#define uncompression_builtin(c) ((c) == c_bzip2 || (c) == c_gzip)
# else
#define uncompression_builtin(c) ((c) == c_gzip)
# endif
#endif
#define uncompression_supported(c) ((c) == c_none || \
		uncompression_builtin(c) || \
		extern_uncompressors[c] != NULL)

enum compression compression_by_suffix(const char *, size_t *);

/**** functions for aptmethod.c ****/

/* we got an pid, check if it is a uncompressor we care for */
retvalue uncompress_checkpid(pid_t, int);
/* still waiting for a client to exit */
bool uncompress_running(void);

typedef retvalue finishaction(void *, const char *, bool /*failed*/);
/* uncompress and call action when finished */
retvalue uncompress_queue_file(const char *, const char *, enum compression, finishaction *, void *);

/**** functions for update.c (uncompressing an earlier downloaded file) ****/

retvalue uncompress_file(const char *, const char *, enum compression);

/**** functions for indexfile.c (uncompressing to memory) and ar.c ****/
// and perhaps also sourceextraction.c

struct compressedfile;

retvalue uncompress_open(/*@out@*/struct compressedfile **, const char *, enum compression);
int uncompress_read(struct compressedfile *, void *buffer, int);
retvalue uncompress_error(/*@const@*/struct compressedfile *);
void uncompress_abort(/*@only@*/struct compressedfile *);
retvalue uncompress_close(/*@only@*/struct compressedfile *);
retvalue uncompress_fdclose(/*@only@*/struct compressedfile *, int *, const char **);

retvalue uncompress_fdopen(/*@out@*/struct compressedfile **, int, off_t, enum compression, int *, const char **);

/**** general initialisation ****/

/* check for existence of external programs */
void uncompressions_check(const char *gunzip, const char *bunzip2, const char *unlzma, const  char *unxz, const char *lunzip, const char *unzstd);

#endif

