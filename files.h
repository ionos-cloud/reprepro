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
retvalue files_done(DB *db);

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

/* dump out all information */
retvalue files_printmd5sums(DB* filesdb);

#endif
