#ifndef __MIRRORER_FILES_H
#define __MIRRORER_FILES_H

#include <db.h>

/* initalize "md5sum and size"-database */
DB *files_initialize(const char *dbpath);

/* Add file's md5sum to database */
int files_add(DB *filesdb,const char *filekey,const char *md5sum_and_size);

/* remove file's md5sum from database */
int files_remove(DB *filesdb,const char *filekey);

/* look for file in database 
 * returns: -2 wrong md5sum, -1: error, 0 not existant, 1 exists*/
int files_check(DB *filesdb,const char *filekey,const char *md5sum_and_size);

/* look for file, calculate its md5sum and add it */
int files_detect(DB *filesdb,const char *mirrordir,const char *filekey);

/* check for file in the database and if not found there, if it can be detected */
int files_expect(DB *filesdb,const char *mirrordir,const char *filekey,const char *md5andsize);

/* dump out all information */
int files_printmd5sums(DB* filesdb);

#endif
