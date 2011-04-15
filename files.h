#ifndef REPREPRO_FILES_H
#define REPREPRO_FILES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_NAMES_H
#include "names.h"
#endif

struct checksums;
struct checksumsarray;

/* Add file's md5sum to database */
retvalue files_add_checksums(const char *, const struct checksums *);

/* remove file's md5sum from database */
retvalue files_remove(const char * /*filekey*/);
/* same but do not call pool_markremoved */
retvalue files_removesilent(const char * /*filekey*/);

/* check for file in the database and if not found there in the pool */
retvalue files_expect(const char *, const struct checksums *, bool warnifreadded);
/* same for multiple files */
retvalue files_expectfiles(const struct strlist *, struct checksums **);

/* check for several files in the database and update information */
retvalue files_checkorimprove(const struct strlist *, struct checksums **);

/* what to do with files */
/* file should already be there, just make sure it is in the database */
#define D_INPLACE      -1
/* copy the file to the given location, return RET_NOTHING, if already in place */
#define D_COPY 		0
/* move the file in place: */
#define D_MOVE 		1
/* move needed and delete unneeded files: */
#define D_DELETE	2

/* Include a given file into the pool
 * return RET_NOTHING, if a file with the same checksums is already there
 * return RET_OK, if copied and added
 * return RET_ERROR_MISSING, if there is no file to copy.
 * return RET_ERROR_WRONG_MD5 if wrong md5sum.
 *  (the original file is not deleted in that case, even if delete is positive)
 */
retvalue files_preinclude(const char *sourcefilename, const char *filekey, /*@null@*//*@out@*/struct checksums **);
retvalue files_checkincludefile(const char *directory, const char *sourcefilename, const char *filekey, struct checksums **);

typedef retvalue per_file_action(void *data, const char *filekey);

/* callback for each registered file */
retvalue files_foreach(per_file_action, void *);

/* check if all files are corect. (skip md5sum if fast is true) */
retvalue files_checkpool(bool /*fast*/);
/* calculate all missing hashes */
retvalue files_collectnewchecksums(void);

/* dump out all information */
retvalue files_printmd5sums(void);
retvalue files_printchecksums(void);

/* look for the given filekey and add it into the filesdatabase */
retvalue files_detect(const char *);

retvalue files_regenerate_filelist(bool redo);

/* hardlink file with known checksums and add it to database */
retvalue files_hardlinkandadd(const char * /*tempfile*/, const char * /*filekey*/, const struct checksums *);

/* RET_NOTHING: file is already there
 * RET_OK : could be added
 * RET_ERROR_WRONG_MD5SUM: filekey is already there with different md5sum */
retvalue files_canadd(const char *filekey, const struct checksums *);

/* make a filekey to a fullfilename. return NULL if OutOfMemory */
static inline char *files_calcfullfilename(const char *filekey) {
	return calc_dirconcat(global.outdir, filekey);
}
off_t files_getsize(const char *);
#endif
