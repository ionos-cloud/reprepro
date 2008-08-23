#ifndef REPREPRO_UNCOMPRESS_H
#define REPREPRO_UNCOMPRESS_H

/* "", ".gz", ... */
extern const char * const uncompression_suffix[c_COUNT];

/* there are two different modes: uncompress a file to memory,
 * or uncompress (possibly multiple files) on the filesystem,
 * controled by aptmethods */

#define uncompression_supported(c) ((c) == c_gzip || (c) == c_none)

/* we got an pid, check if it is a uncompressor we care for */
retvalue uncompress_checkpid(pid_t, int);
/* still waiting for a client to exit */
bool uncompress_running(void);

typedef retvalue finishaction(void *, const char *, bool);
/* uncompress and call action when finished */
retvalue uncompress_queue_file(const char *, const char *, enum compression, finishaction *, void *, bool *);

retvalue uncompress_file(const char *, const char *, enum compression);

/* check for existance of external programs */
void uncompressions_check(void);

#endif

