/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2003 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <config.h>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <db.h>
#include "error.h"
#include "mprintf.h"
#include "md5sum.h"
#include "dirs.h"
#include "packages.h"
#include "names.h"
#include "files.h"
#include "copyfile.h"

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

extern int verbose;

/* initalize "md5sum and size"-database */
DB *files_initialize(const char *dbpath) {
	DB *dbp;
	int dbret;
	char *filename;
	retvalue r;

	
	filename = mprintf("%s/files.db",dbpath);
	if( !filename )
		return NULL;
	r = dirs_make_parent(filename);
	if( RET_WAS_ERROR(r) ) {
		free(filename);
		return NULL;
	}
	if ((dbret = db_create(&dbp, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(dbret));
		free(filename);
		return NULL;
	}
	if ((dbret = dbp->open(dbp, filename, "md5sums", DB_BTREE, DB_CREATE, 0664)) != 0) {
		dbp->err(dbp, dbret, "%s", filename);
		(void)dbp->close(dbp,0);
		free(filename);
		return NULL;
	}
	free(filename);
	return dbp;
}

/* release the files-database initialized got be files_initialize */
retvalue files_done(DB *db) {
	int dberr;
	/* just in case we want something here later */
	dberr = db->close(db,0);
	if( dberr != 0 )
		return RET_DBERR(dberr);
	else
		return RET_OK;
}

/* Add file's md5sum to database */
retvalue files_add(DB *filesdb,const char *filekey,const char *md5sum_and_size) {
	int dbret;
	DBT key,data;

	SETDBT(key,filekey);
	SETDBT(data,md5sum_and_size);
	if( (dbret = filesdb->put(filesdb, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose>1)
			printf("db: %s: file added.\n", (const char *)key.data);
		return RET_OK;
	} else {
		filesdb->err(filesdb, dbret, "files.db:md5sums:");
		return RET_DBERR(dbret);
	}
}

/* remove file's md5sum from database */
retvalue files_remove(DB *filesdb,const char *filekey) {
	int dbret;
	DBT key;

	SETDBT(key,filekey);
	if ((dbret = filesdb->del(filesdb, NULL, &key, 0)) == 0) {
		if( verbose>1 )
			printf("db: %s: file forgotten.\n", (const char *)key.data);
		return RET_OK;
	} else {
		filesdb->err(filesdb, dbret, "files.db:md5sums:");
		return RET_DBERR(dbret);
	}
}

/* look for file in database
 * returns: -2 wrong md5sum, -1: error, 0 not existant, 1 exists*/
retvalue files_check(DB *filesdb,const char *filekey,const char *md5sum_and_size) {
	int dbret;
	DBT key,data;

	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (dbret = filesdb->get(filesdb, NULL, &key, &data, 0)) == 0){
		if( strcmp(md5sum_and_size,data.data) != 0 ) {
			fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(expect: '%s', database:'%s')!\n",filekey,md5sum_and_size,(char*)data.data);
			return RET_ERROR_WRONG_MD5;
		}
		return RET_OK;
	} else if( dbret == DB_NOTFOUND ){
		return RET_NOTHING;
	} else {
		 filesdb->err(filesdb, dbret, "files.db:");
		 return RET_DBERR(dbret);
	}
}

static retvalue files_calcmd5(char **md5andsize,const char *mirrordir,const char *filekey) {
	char *filename;
	retvalue ret;

	filename = calc_fullfilename(mirrordir,filekey);

	if( !filename )
		return RET_ERROR_OOM;

	*md5andsize = NULL;
	ret = md5sum_and_size(md5andsize,filename,0);

	if( ret == RET_NOTHING ) {
		fprintf(stderr,"Error accessing file \"%s\": %m\n",filename);
		free(filename);
		return RET_ERROR;

	}
	if( RET_WAS_ERROR(ret) ) {
		fprintf(stderr,"Error checking file \"%s\": %m\n",filename);
		free(filename);
		return ret;
	}
	free(filename);
	if( verbose > 20 ) {
		fprintf(stderr,"Md5sum of '%s' is '%s'.\n",filename,*md5andsize);
	}
	return ret;

}

/* look for file, calculate its md5sum and add it */
retvalue files_detect(DB *filesdb,const char *mirrordir,const char *filekey) {
	char *md5andsize;	
	retvalue ret;

	ret = files_calcmd5(&md5andsize,mirrordir,filekey);
	if( RET_WAS_ERROR(ret) )
		return ret;

	ret = files_check(filesdb,filekey,md5andsize);

	if( RET_WAS_ERROR(ret) ) {
		free(md5andsize);
		return ret;
	}

	if( ret == RET_NOTHING ) {
		ret = files_add(filesdb,filekey,md5andsize);
		free(md5andsize);
		if( RET_WAS_ERROR(ret) )
			return ret;
		return RET_OK;
	}

	return ret;
}

/* check for file in the database and if not found there, if it can be detected */
int files_expect(DB *filesdb,const char *mirrordir,const char *filekey,const char *md5andsize) {
	char *filename;
	retvalue ret;
	char *realmd5andsize;

	/* check in database */
	ret = files_check(filesdb,filekey,md5andsize);
	if( ret != RET_NOTHING ) {
		/* if error or already in database, nothing to do */
		return ret;
	}

	/* look for the file */
	
	filename = calc_fullfilename(mirrordir,filekey);
	if( !filename )
		return RET_ERROR_OOM;

	realmd5andsize = NULL;
	ret = md5sum_and_size(&realmd5andsize,filename,0);

	if( RET_WAS_ERROR(ret) ) {
		// TODO: move this check to md5sum_and_size, should cause RET_NOTHING
		if( ret != RET_ERRNO(EACCES) && ret != RET_ERRNO(EPERM)) {
			fprintf(stderr,"Error accessing file \"%s\": %m(%d)\n",filename,ret);
			free(filename);
			free(realmd5andsize);
			return ret;
		} else {
			free(filename);
			free(realmd5andsize);
			return RET_NOTHING;
		}
	}
	free(filename);
	if( ret == RET_NOTHING ) {
		free(realmd5andsize);
		return ret;
	}

	if( strcmp(md5andsize,realmd5andsize) != 0 ) {
		fprintf(stderr,"File \"%s\" has other md5sum than expected!\n",filekey);
		fprintf(stderr,"File \"%s\" had other md5sum (%s) than expected(%s)!\n",filekey,realmd5andsize,md5andsize);
		free(realmd5andsize);
		return RET_ERROR_WRONG_MD5;
	}
	free(realmd5andsize);

	/* add file to database */

	ret = files_add(filesdb,filekey,md5andsize);
	return ret;
}

/* dump out all information */
int files_printmd5sums(DB* filesdb) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result;

	cursor = NULL;
	if( (dbret = filesdb->cursor(filesdb,NULL,&cursor,0)) != 0 ) {
		filesdb->err(filesdb, dbret, "files.db:md5sums:");
		return RET_DBERR(dbret);
	}
	CLEARDBT(key);	
	CLEARDBT(data);	
	result = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		printf("%s %s\n",(const char*)key.data,(const char*)data.data);
		result = RET_OK;
	}
	if( dbret != DB_NOTFOUND ) {
		filesdb->err(filesdb, dbret, "files.db:md5sums:");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		filesdb->err(filesdb, dbret, "files.db:md5sums:");
		return RET_DBERR(dbret);
	}
	return result;
}

// retvalue files_foreach(DB* filesdb,per_file_action action,void *data);

/* callback for each registered file */
retvalue files_foreach(DB* filesdb,per_file_action action,void *privdata) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result,r;

	cursor = NULL;
	if( (dbret = filesdb->cursor(filesdb,NULL,&cursor,0)) != 0 ) {
		filesdb->err(filesdb, dbret, "files.db:md5sums:");
		return RET_DBERR(dbret);
	}
	CLEARDBT(key);	
	CLEARDBT(data);	
	result = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		r = action(privdata,(const char*)key.data,(const char*)data.data);
		RET_UPDATE(result,r);
	}
	if( dbret != DB_NOTFOUND ) {
		filesdb->err(filesdb, dbret, "files.db:md5sums:");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		filesdb->err(filesdb, dbret, "files.db:md5sums:");
		return RET_DBERR(dbret);
	}
	return result;
}

/* Copy file <origfilename> to <mirrordir>/<filekey> and add it to
 * the database <filesdb>. Return RET_ERROR_WRONG_MD5 if already there 
 * with other md5sum, return other error when the file does not exists
 * or the database had an error. return RET_NOTHING, if already there
 * with correct md5sum. Return <md5andsize> with the data of this file,
 * if no error (that is if RET_OK or RET_NOTHING) */
retvalue files_checkin(DB *filesdb,const char *mirrordir,const char *filekey,
		const char *origfilename, char **md5andsize) {

	int dbret;
	DBT key,data;
	retvalue r;

	/* First check for possible entries in the database... */

	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (dbret = filesdb->get(filesdb, NULL, &key, &data, 0)) == 0){
		if( verbose > 10 ) {
			fprintf(stderr,"Database says: '%s' already here as '%s'!\n",filekey,(const char *)data.data);
		}
		r = files_calcmd5(md5andsize,mirrordir,filekey);
		if( RET_WAS_ERROR(r) )
			return r;
		if( strcmp(*md5andsize,data.data) != 0 ) {
			fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(expect: '%s', database:'%s')!\n",filekey,*md5andsize,(const char *)data.data);
			free(*md5andsize);
			*md5andsize = NULL;
			return RET_ERROR_WRONG_MD5;
		} else if( verbose >= 0 ) {
			fprintf(stderr,"'%s' is already registered in the database, so doing nothing!\n",filekey);
			return RET_NOTHING;
		}
	} else if( dbret != DB_NOTFOUND ){
		 filesdb->err(filesdb, dbret, "files.db:");
		 return RET_DBERR(dbret);
	}

	/* File is not yet in database, check if perhaps in tree */

	// TODO
	
	/* copy file in and calculate it's md5sum */
	
	r = copyfile(mirrordir,filekey,origfilename);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error copying file %s to %s/%s\n",origfilename,mirrordir,filekey);
		return r;
	}

	r = files_calcmd5(md5andsize,mirrordir,filekey);
	if( RET_WAS_ERROR(r) )
		return r;

	r = files_add(filesdb,filekey,*md5andsize);
	if( RET_WAS_ERROR(r) ) {
		free(*md5andsize);
		*md5andsize = NULL;
		return r;
	}
	return RET_OK;
}
