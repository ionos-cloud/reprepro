#ifndef REPREPRO_FILES_H
#define REPREPRO_FILES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

struct checksums;
struct checksumsarray;

/* Add file's md5sum to database */
retvalue files_add_checksums(struct database *, const char *, const struct checksums *);

/* remove file's md5sum from database */
retvalue files_remove(struct database *, const char *filekey, bool ignoremissing);

/* delete the file and remove its md5sum from database,
 * also try to rmdir empty directories it is in if rmdirs is true */
retvalue files_deleteandremove(struct database *, const char *filekey, bool rmdirs, bool ignoremissing);

/* check for file in the database and if not found there in the pool */
retvalue files_expect(struct database *, const char *, const struct checksums *);
/* same for multiple files */
retvalue files_expectfiles(struct database *, const struct strlist *, struct checksums **);

/* print missing files */
retvalue files_printmissing(struct database *, const struct strlist *filekeys, const struct checksumsarray *);

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
retvalue files_preinclude(struct database *, const char *sourcefilename, const char *filekey, /*@null@*//*@out@*/struct checksums **, /*@out@*/bool *);
retvalue files_checkincludefile(struct database *, const char *directory, const char *sourcefilename, const char *filekey, struct checksums **, /*@out@*/bool *);

typedef retvalue per_file_action(void *data, const char *filekey);

/* callback for each registered file */
retvalue files_foreach(struct database *,per_file_action action,void *data);

/* check if all files are corect. (skip md5sum if fast is true) */
retvalue files_checkpool(struct database *, bool fast);
/* calculate all missing hashes */
retvalue files_collectnewchecksums(struct database *);

/* dump out all information */
retvalue files_printmd5sums(struct database *);
retvalue files_printchecksums(struct database *);

/* look for the given filekey and add it into the filesdatabase */
retvalue files_detect(struct database *,const char *filekey);

retvalue files_regenerate_filelist(struct database *, bool redo);

/* hardlink file with known checksums and add it to database */
retvalue files_hardlinkandadd(struct database *, const char *tempfile, const char *filekey, const struct checksums *);

/* check if file is already there (RET_NOTHING) or could be added (RET_OK)
 * or RET_ERROR_WRONG_MD5SUM if filekey is already there with different md5sum */
retvalue files_canadd(struct database *, const char *filekey, const struct checksums *);

#endif
