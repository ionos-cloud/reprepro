#ifndef REPREPRO_FILES_H
#define REPREPRO_FILES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

typedef struct s_filesdb *filesdb;

/* initalize "md5sum and size"-database */
retvalue files_initialize(/*@out@*/ filesdb *filesdb,const char *dbpath,const char *mirrordir);

/* release the files-database initialized got be files_initialize */
retvalue files_done(/*@only@*/filesdb db);

/* Add file's md5sum to database */
retvalue files_add(filesdb filesdb,const char *filekey,const char *md5sum);

/* remove file's md5sum from database */
retvalue files_remove(filesdb filesdb,const char *filekey);

/* delete the file and remove its md5sum from database,
 * also try to rmdir empty directories it is in if rmdirs is true */
retvalue files_deleteandremove(filesdb filesdb,const char *filekey, bool_t rmdirs);

/* check for file in the database and if not found there in the pool */
retvalue files_expect(filesdb filesdb,const char *filekey,const char *md5sum);
/* same for multiple files */
retvalue files_expectfiles(filesdb filesdb,const struct strlist *filekeys,const struct strlist *md5sums);

/* print missing files */
retvalue files_printmissing(filesdb filesdb,const struct strlist *filekeys,const struct strlist *md5sums,const struct strlist *origfiles);

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
 * return RET_NOTHING, if done,
 * 3) copy or move file (depending on delete) file to destination,
 * making sure it has the correct <md5sum> if given, 
 * or computing it, if <claculatemd5sum> is given.
 * return RET_OK, if done,
 * return RET_ERROR_MISSING, if there is no file to copy.
 * return RET_ERROR_WRONG_MD5 if wrong md5sum.
 *  (the original file is not deleted in that case, even if delete is positive)
 * 4) add it to the database
 */
retvalue files_include(filesdb filesdb,const char *sourcefilename,const char *filekey, /*@null@*/const char *md5sum, /*@null@*/char **calculatedmd5sum, int delete);

/* same as above, but use sourcedir/basename instead of sourcefilename */
retvalue files_includefile(filesdb filesdb,const char *sourcedir,const char *basename, const char *filekey, const char *md5sum, /*@null@*/char **calculatedmd5sum, int delete);

/* the same, but with multiple files */
retvalue files_includefiles(filesdb filesdb,const char *sourcedir,const struct strlist *basenames, const struct strlist *filekeys, const struct strlist *md5sums, int delete);

typedef retvalue per_file_action(void *data,const char *filekey,const char *md5sum);

/* callback for each registered file */
retvalue files_foreach(filesdb filesdb,per_file_action action,void *data);

/* check if all files are corect. (skip md5sum if fast is true) */
retvalue files_checkpool(filesdb filesdb,bool_t fast);

/* dump out all information */
retvalue files_printmd5sums(filesdb filesdb);

/* concat mirrordir. return NULL if OutOfMemory */
char *files_calcfullfilename(const filesdb filesdb,const char *filekey);

/* look for the given filekey and add it into the filesdatabase */
retvalue files_detect(filesdb db,const char *filekey);

#endif
