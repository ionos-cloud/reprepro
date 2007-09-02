#ifndef REPREPRO_FILES_H
#define REPREPRO_FILES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#include "filelist.h"

struct filesdb;

/* initalize "md5sum and size"-database */
retvalue files_initialize(/*@out@*/struct filesdb **,struct database *,const char *mirrordir);

/* release the files-database initialized got be files_initialize */
retvalue files_done(/*@only@*/struct filesdb *);

/* Add file's md5sum to database */
retvalue files_add(struct database *,const char *filekey,const char *md5sum);

/* remove file's md5sum from database */
retvalue files_remove(struct database *, const char *filekey, bool ignoremissing);

/* delete the file and remove its md5sum from database,
 * also try to rmdir empty directories it is in if rmdirs is true */
retvalue files_deleteandremove(struct database *, const char *filekey, bool rmdirs, bool ignoremissing);

/* check for file in the database and if not found there in the pool */
retvalue files_expect(struct database *,const char *filekey,const char *md5sum);
/* same for multiple files */
retvalue files_expectfiles(struct database *,const struct strlist *filekeys,const struct strlist *md5sums);

/* print missing files */
retvalue files_printmissing(struct database *,const struct strlist *filekeys,const struct strlist *md5sums,const struct strlist *origfiles);

/* what to do with files */
/* file should already be there, just make sure it is in the database */
#define D_INPLACE      -1
/* copy the file to the given location, return RET_NOTHING, if already in place */
#define D_COPY 		0
/* move the file in place: */
#define D_MOVE 		1
/* move needed and delete unneeded files: */
#define D_DELETE	2

/* Include a given file into the pool. i.e.:
 * 1) if <md5dum> != NULL
 *    check if <filekey> with <md5sum> is already there,
 *    return RET_NOTHING if it is.
 *    return n RET_ERROR_WRONG_MD5 if wrong md5sum.
 * 2) Look if there is already a file in the pool with correct md5sum.
 * and add it to the database if yes.
 * return RET_OK, if done, (and set *calculatedmd5sum)
 * 3) copy or move file (depending on delete) file to destination,
 * making sure it has the correct <md5sum> if given,
 * or computing it, if <claculatemd5sum> is given.
 * return RET_OK, if done,
 * return RET_ERROR_MISSING, if there is no file to copy.
 * return RET_ERROR_WRONG_MD5 if wrong md5sum.
 *  (the original file is not deleted in that case, even if delete is positive)
 * 4) add it to the database
 */
retvalue files_include(struct database *,const char *sourcefilename,const char *filekey, /*@null@*/const char *md5sum, /*@null@*/char **calculatedmd5sum, int delete);

/* same as above, but use sourcedir/basename instead of sourcefilename */
retvalue files_includefile(struct database *,const char *sourcedir,const char *basename, const char *filekey, const char *md5sum, /*@null@*/char **calculatedmd5sum, int delete);

/* the same, but with multiple files */
retvalue files_includefiles(struct database *,const char *sourcedir,const struct strlist *basenames, const struct strlist *filekeys, const struct strlist *md5sums, int delete);

typedef retvalue per_file_action(void *data,const char *filekey,const char *md5sum);

/* callback for each registered file */
retvalue files_foreach(struct database *,per_file_action action,void *data);

/* check if all files are corect. (skip md5sum if fast is true) */
retvalue files_checkpool(struct database *, bool fast);

/* dump out all information */
retvalue files_printmd5sums(struct database *);

/* concat mirrordir. return NULL if OutOfMemory */
char *files_calcfullfilename(const struct database *,const char *filekey);

/* look for the given filekey and add it into the filesdatabase */
retvalue files_detect(struct database *,const char *filekey);

retvalue files_getfilelist(struct database *,const char *filekey,const struct filelist_package *package, struct filelist_list *filelist);
retvalue files_genfilelist(struct database *,const char *filekey,const struct filelist_package *package, struct filelist_list *filelist);
retvalue files_regenerate_filelist(struct database *, bool redo);
retvalue files_addfilelist(struct database *,const char *filekey,const char *filelist);

/* hardlink file with known md5sum and add it to database */
retvalue files_hardlink(struct database *,const char *tempfile, const char *filekey,const char *md5sum);
/* check if file is already there (RET_NOTHING) or could be added (RET_OK)
 * or RET_ERROR_WRONG_MD5SUM if filekey is already there with different md5sum */
retvalue files_ready(struct database *,const char *filekey,const char *md5sum);

#endif
