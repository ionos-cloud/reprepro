#ifndef __MIRRORER_FILES_H
#define __MIRRORER_FILES_H

#include <db.h>
#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* initalize "md5sum and size"-database */
DB *files_initialize(const char *dbpath);

/* release the files-database initialized got be files_initialize */
retvalue files_done( DB *db);

/* Add file's md5sum to database */
retvalue files_add(DB *filesdb,const char *filekey,const char *md5sum_and_size);

/* remove file's md5sum from database */
retvalue files_remove(DB *filesdb,const char *filekey);

/* look for file in database 
 * returns: -2 wrong md5sum, -1: error, 0 not existant, 1 exists*/
retvalue files_check(DB *filesdb,const char *filekey,const char *md5sum_and_size);

/* look for file, calculate its md5sum and add it */
retvalue files_detect(DB *filesdb,const char *mirrordir,const char *filekey);

/* check for file in the database and if not found there, if it can be detected */
retvalue files_expect(DB *filesdb,const char *mirrordir,const char *filekey,const char *md5andsize);

/* print missing files */
retvalue files_printmissing(const char *mirrordir,DB *filesdb,const struct strlist *filekeys,const struct strlist *md5sums,const struct strlist *origfiles);
/* check for several files in the database and in the pool if missing */
retvalue files_expectfiles(const char *mirrordir,DB *filesdb,const struct strlist *filekeys,const struct strlist *md5sums);

/* Copy file <origfilename> to <mirrordir>/<filekey> and add it to
 * the database <filesdb>. Return RET_ERROR_WRONG_MD5 if already there 
 * with other md5sum, return other error when the file does not exists
 * or the database had an error. return RET_NOTHING, if already there
 * with correct md5sum. Return <md5andsize> with the data of this file,
 * if no error (that is if RET_OK or RET_NOTHING) */
retvalue files_checkin(const char *mirrordir,DB *filesdb,const char *filekey,
		const char *origfilename, char **md5andsize);

/* Make sure filekeys with md5sums are in the pool. If not copy from
 * sourcedir/file where file is the entry from files */
retvalue files_checkinfiles(const char *mirrordir,DB *filesdb,const char *sourcedir,const struct strlist *basefilenames,const struct strlist *filekeys,const struct strlist *md5sums);
/* The same for a single file: */
retvalue files_checkinfile(const char *mirrordir,DB *filesdb,const char *sourcedir,const char *basename,const char *filekey,const char *md5sum);

typedef retvalue per_file_action(void *data,const char *filekey,const char *md5andsize);

/* callback for each registered file */
retvalue files_foreach(DB* filesdb,per_file_action action,void *data);

/* dump out all information */
retvalue files_printmd5sums(DB* filesdb);

#endif
