#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <db.h>
#include "md5sum.h"
#include "dirs.h"
#include "packages.h"
#include "names.h"
#include "files.h"

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

extern int verbose;

/* initalize "md5sum and size"-database */
DB *files_initialize(const char *dbpath) {
	DB *dbp;
	int ret;
	char *filename;

	
	asprintf(&filename,"%s/files.db",dbpath);
	if( make_parent_dirs(filename) < 0 ) {
		free(filename);
		return NULL;
	}
	if ((ret = db_create(&dbp, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		free(filename);
		return NULL;
	}
	if ((ret = dbp->open(dbp, filename, "md5sums", DB_BTREE, DB_CREATE, 0664)) != 0) {
		dbp->err(dbp, ret, "%s", filename);
		dbp->close(dbp,0);
		free(filename);
		return NULL;
	}                     
	free(filename);
	return dbp;
}


/* Add file's md5sum to database */
int files_add(DB *filesdb,const char *filekey,const char *md5sum_and_size) {
	int ret;
	DBT key,data;

	SETDBT(key,filekey);
	SETDBT(data,md5sum_and_size);
	if ((ret = filesdb->put(filesdb, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose>1)
			printf("db: %s: file added.\n", (const char *)key.data);
	} else {
		filesdb->err(filesdb, ret, "files.db:md5sums:");
	}
	return ret;
}

/* remove file's md5sum from database */
int files_remove(DB *filesdb,const char *filekey) {
	int ret;
	DBT key;

	SETDBT(key,filekey);
	if ((ret = filesdb->del(filesdb, NULL, &key, 0)) == 0) {
		if( verbose>1 )
			printf("db: %s: file forgotten.\n", (const char *)key.data);
	} else {
		filesdb->err(filesdb, ret, "files.db:md5sums:");
	}
	return ret;
}

/* look for file in database 
 * returns: -2 wrong md5sum, -1: error, 0 not existant, 1 exists*/
int files_check(DB *filesdb,const char *filekey,const char *md5sum_and_size) {
	int ret;
	DBT key,data;

	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (ret = filesdb->get(filesdb, NULL, &key, &data, 0)) == 0){
		if( strcmp(md5sum_and_size,data.data) != 0 ) {
			fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n",filekey);
			return -2;
		}
		return 1;
	} else if( ret == DB_NOTFOUND ){
		return 0;
	} else {
		 filesdb->err(filesdb, ret, "files.db:");
		 return -1;
	}
}

/* look for file, calculate its md5sum and add it */
int files_detect(DB *filesdb,const char *mirrordir,const char *filekey) {
	char *filename,*md5andsize;	
	int ret;

	filename = calc_fullfilename(mirrordir,filekey);

	if( !filename )
		return -1;

	ret = md5sum_and_size(&md5andsize,filename,0);

	if( ret != 0 ) {
		fprintf(stderr,"Error accessing file \"%s\": %m\n",filename);
		free(filename);
		free(md5andsize);
		return -1;
	}
	free(filename);

	ret = files_check(filesdb,filekey,md5andsize);

	if( ret < 0 ) {
		free(md5andsize);
		return ret;
	}

	if( ret == 0 ) {
		ret = files_add(filesdb,filekey,md5andsize);
		free(md5andsize);
		if( ret != 0 )
			return -1;
		return 1;
	}

	return 0;
}

/* check for file in the database and if not found there, if it can be detected */
int files_expect(DB *filesdb,const char *mirrordir,const char *filekey,const char *md5andsize) {
	char *filename;
	int ret;
	char *realmd5andsize;

	/* check in database */
	ret = files_check(filesdb,filekey,md5andsize);
	if( ret != 0 ) {
		return ret;
	}

	/* look for the file */
	
	filename = calc_fullfilename(mirrordir,filekey);
	if( !filename )
		return -120;

	ret = md5sum_and_size(&realmd5andsize,filename,0);

	if( ret != 0 ) {
		if( ret != -EACCES && ret != -EPERM)
			fprintf(stderr,"Error accessing file \"%s\": %m(%d)\n",filename,ret);
		free(filename);
		free(realmd5andsize);
		return (ret==-EACCES||ret==-EPERM)?0:-1;
	}
	free(filename);

	if( strcmp(md5andsize,realmd5andsize) != 0 ) {
		fprintf(stderr,"File \"%s\" has other md5sum than expected!\n",filekey);
		free(realmd5andsize);
		return -2;
	}
	free(realmd5andsize);

	/* add file to database */

	ret = files_add(filesdb,filekey,md5andsize);
	if( ret != 0 )
		return ret;
	return 1;
}

/* dump out all information */
int files_printmd5sums(DB* filesdb) {
	DBC *cursor;
	DBT key,data;
	int ret;

	cursor = NULL;
	if( (ret = filesdb->cursor(filesdb,NULL,&cursor,0)) != 0 ) {
		filesdb->err(filesdb, ret, "files.db:md5sums:");
		return -1;
	}
	CLEARDBT(key);	
	CLEARDBT(data);	
	while( (ret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		printf("%s %s\n",(const char*)key.data,(const char*)data.data);		
		
	}
	if( ret != DB_NOTFOUND ) {
		filesdb->err(filesdb, ret, "files.db:md5sums:");
		return -1;
	}
	if( (ret = cursor->c_close(cursor)) != 0 ) {
		filesdb->err(filesdb, ret, "files.db:md5sums:");
		return -1;
	}
	return 0;
}


