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
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <db.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "md5sum.h"
#include "dirs.h"
#include "names.h"
#include "files.h"
#include "copyfile.h"

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

extern int verbose;

/* initalize "md5sum and size"-database */
retvalue files_initialize(filesdb *fdb,const char *dbpath,const char *mirrordir) {
	filesdb db;
	int dbret;
	char *filename;
	retvalue r;

	db = malloc(sizeof(struct s_filesdb));
	if( db == NULL )
		return RET_ERROR_OOM;
	db->mirrordir = strdup(mirrordir);
	if( db->mirrordir == NULL ) {
		free(db);
		return RET_ERROR_OOM;
	}

	filename = calc_dirconcat(dbpath,"files.db");
	if( !filename ) {
		free(db->mirrordir);
		free(db);
		return RET_ERROR_OOM;
	}
	r = dirs_make_parent(filename);
	if( RET_WAS_ERROR(r) ) {
		free(filename);
		free(db->mirrordir);
		free(db);
		return r;
	}
	if ((dbret = db_create(&db->database, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(dbret));
		free(filename);
		free(db->mirrordir);
		free(db);
		return RET_DBERR(dbret);
	}
	if ((dbret = db->database->open(db->database, filename, "md5sums", DB_BTREE, DB_CREATE, 0664)) != 0) {
		db->database->err(db->database, dbret, "%s", filename);
		(void)db->database->close(db->database,0);
		free(filename);
		free(db->mirrordir);
		free(db);
		return RET_DBERR(dbret);
	}
	free(filename);
	*fdb = db;
	return RET_OK;
}

/* release the files-database initialized got be files_initialize */
retvalue files_done(filesdb db) {
	int dberr;
	/* just in case we want something here later */
	dberr = db->database->close(db->database,0);
	if( dberr != 0 )
		return RET_DBERR(dberr);
	else
		return RET_OK;
}

/* Add file's md5sum to database */
retvalue files_add(filesdb db,const char *filekey,const char *md5sum) {
	int dbret;
	DBT key,data;

	SETDBT(key,filekey);
	SETDBT(data,md5sum);
	if( (dbret = db->database->put(db->database, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose>1)
			printf("db: %s: file added.\n", (const char *)key.data);
		return RET_OK;
	} else {
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
}

/* remove file's md5sum from database */
retvalue files_remove(filesdb db,const char *filekey) {
	int dbret;
	DBT key;

	SETDBT(key,filekey);
	if ((dbret = db->database->del(db->database, NULL, &key, 0)) == 0) {
		if( verbose>1 )
			printf("db: %s: file forgotten.\n", (const char *)key.data);
		return RET_OK;
	} else {
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
}

/* look for file in database
 * returns: -2 wrong md5sum, -1: error, 0 not existant, 1 exists*/
retvalue files_check(filesdb db,const char *filekey,const char *md5sum) {
	int dbret;
	DBT key,data;

	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (dbret = db->database->get(db->database, NULL, &key, &data, 0)) == 0){
		if( strcmp(md5sum,data.data) != 0 ) {
			fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(expect: '%s', database:'%s')!\n",filekey,md5sum,(char*)data.data);
			return RET_ERROR_WRONG_MD5;
		}
		return RET_OK;
	} else if( dbret == DB_NOTFOUND ){
		return RET_NOTHING;
	} else {
		 db->database->err(db->database, dbret, "files.db:");
		 return RET_DBERR(dbret);
	}
}

static retvalue files_calcmd5(char **md5sum,const char *filename) {
	retvalue ret;

	*md5sum = NULL;
	ret = md5sum_read(filename,md5sum);

	if( ret == RET_NOTHING ) {
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(ret) ) {
		return ret;
	}
	if( verbose > 20 ) {
		fprintf(stderr,"Md5sum of '%s' is '%s'.\n",filename,*md5sum);
	}
	return ret;

}
static retvalue files_calcmd5sum(filesdb filesdb,const char *filekey,char **md5sum) {
	char *filename;
	retvalue ret;

	filename = calc_fullfilename(filesdb->mirrordir,filekey);

	if( !filename )
		return RET_ERROR_OOM;

	ret = files_calcmd5(md5sum,filename);
	free(filename);
	return ret;

}

/* look for file, calculate its md5sum and add it */
retvalue files_detect(filesdb filesdb,const char *filekey) {
	char *md5sum;	
	retvalue ret;

	ret = files_calcmd5sum(filesdb,filekey,&md5sum);
	if( RET_WAS_ERROR(ret) )
		return ret;

	ret = files_check(filesdb,filekey,md5sum);

	if( RET_WAS_ERROR(ret) ) {
		free(md5sum);
		return ret;
	}

	if( ret == RET_NOTHING ) {
		ret = files_add(filesdb,filekey,md5sum);
		free(md5sum);
		if( RET_WAS_ERROR(ret) )
			return ret;
		return RET_OK;
	}

	return ret;
}

/* check for file in the database and if not found there, if it can be detected */
int files_expect(filesdb db,const char *filekey,const char *md5sum) {
	char *filename;
	retvalue ret;
	char *realmd5sum;

	/* check in database */
	ret = files_check(db,filekey,md5sum);
	if( ret != RET_NOTHING ) {
		/* if error or already in database, nothing to do */
		return ret;
	}

	/* look for the file */
	
	filename = calc_fullfilename(db->mirrordir,filekey);
	if( !filename )
		return RET_ERROR_OOM;

	ret = md5sum_read(filename,&realmd5sum);
	free(filename);
	if( ret == RET_NOTHING || RET_WAS_ERROR(ret) )
		return ret;

	if( strcmp(md5sum,realmd5sum) != 0 ) {
		fprintf(stderr,"File \"%s\" had other md5sum (%s) than expected(%s)!\n",filekey,realmd5sum,md5sum);
		free(realmd5sum);
		return RET_ERROR_WRONG_MD5;
	}
	free(realmd5sum);

	/* add file to database */

	ret = files_add(db,filekey,md5sum);
	return ret;
}

/* check for several files in the database and in the pool if missing */
retvalue files_expectfiles(filesdb filesdb,const struct strlist *filekeys,const struct strlist *md5sums) {
	int i;
	retvalue r;

	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *filekey = filekeys->values[i];
		const char *md5sum = md5sums->values[i];

		r = files_expect(filesdb,filekey,md5sum);
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
		if( r == RET_NOTHING ) {
			/* File missing */
			fprintf(stderr,"Missing file %s\n",filekey);
			return RET_ERROR;
		}
	}
	return RET_OK;
}

/* print missing files */
retvalue files_printmissing(filesdb db,const struct strlist *filekeys,const struct strlist *md5sums,const struct strlist *origfiles) {
	int i;
	retvalue ret,r;

	ret = RET_NOTHING;
	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *filekey = filekeys->values[i];
		const char *md5sum = md5sums->values[i];
		const char *origfile = origfiles->values[i];

		r = files_expect(db,filekey,md5sum);
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
		if( r == RET_NOTHING ) {
			/* File missing */
			printf("%s %s/%s\n",origfile,db->mirrordir,filekey);
			RET_UPDATE(ret,RET_OK);
		} else
			RET_UPDATE(ret,r);
	}
	return ret;
}

/* dump out all information */
int files_printmd5sums(filesdb db) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result;

	cursor = NULL;
	if( (dbret = db->database->cursor(db->database,NULL,&cursor,0)) != 0 ) {
		db->database->err(db->database, dbret, "files.db:");
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
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
	return result;
}

// retvalue files_foreach(DB* filesdb,per_file_action action,void *data);

/* callback for each registered file */
retvalue files_foreach(filesdb db,per_file_action action,void *privdata) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result,r;

	cursor = NULL;
	if( (dbret = db->database->cursor(db->database,NULL,&cursor,0)) != 0 ) {
		db->database->err(db->database, dbret, "files.db:");
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
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
	return result;
}

/* Copy file <origfilename> to <mirrordir>/<filekey> and add it to
 * the database <filesdb>. Return RET_ERROR_WRONG_MD5 if already there 
 * with other md5sum, return other error when the file does not exists
 * or the database had an error. return RET_NOTHING, if already there
 * with correct md5sum. Return <md5sum> with the data of this file,
 * if no error (that is if RET_OK or RET_NOTHING) */
retvalue files_checkin(filesdb db,const char *filekey,
		const char *origfilename, char **md5sum) {

	int dbret;
	DBT key,data;
	retvalue r;

	/* First check for possible entries in the database... */

	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (dbret = db->database->get(db->database, NULL, &key, &data, 0)) == 0){
		if( verbose > 10 ) {
			fprintf(stderr,"Database says: '%s' already here as '%s'!\n",filekey,(const char *)data.data);
		}
		r = files_calcmd5(md5sum,origfilename);
		if( RET_WAS_ERROR(r) )
			return r;
		if( strcmp(*md5sum,data.data) != 0 ) {
			fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(expect: '%s', database:'%s')!\n",filekey,*md5sum,(const char *)data.data);
			free(*md5sum);
			*md5sum = NULL;
			return RET_ERROR_WRONG_MD5;
		} else if( verbose >= 0 ) {
			fprintf(stderr,"'%s' is already registered in the database, so doing nothing!\n",filekey);
			return RET_NOTHING;
		}
	} else if( dbret != DB_NOTFOUND ){
		 db->database->err(db->database, dbret, "files.db:");
		 return RET_DBERR(dbret);
	}

	/* File is not yet in database, check if perhaps in tree */

	// TODO
	
	/* copy file in and calculate it's md5sum */
	
	r = copyfile_getmd5(db->mirrordir,filekey,origfilename,md5sum);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error copying file %s to %s/%s\n",origfilename,db->mirrordir,filekey);
		return r;
	}

	r = files_add(db,filekey,*md5sum);
	if( RET_WAS_ERROR(r) ) {
		free(*md5sum);
		*md5sum = NULL;
		return r;
	}
	return RET_OK;
}

// This gets slowly a bit messy, usages of files_insert should be
// changed in near future to use this. (When update is rewritten
// anyways). Problem is some things know the md5sum and other need
// it, this is for things that know it...
retvalue files_checkinfile(filesdb db,const char *sourcedir,const char *basename,const char *filekey,const char *md5sum) {
	DBT key,data;
	int dbret;
	retvalue r;

	/* First check for possible entries in the database... */
	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (dbret = db->database->get(db->database, NULL, &key, &data, 0)) == 0){
		if( verbose > 10 ) {
			fprintf(stderr,"Database says: '%s' already here as '%s'!\n",filekey,(const char *)data.data);
		}
		if( strcmp( (const char*)data.data, md5sum ) != 0 ) {
			fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(expect: '%s', database:'%s')!\n",filekey,md5sum,(const char *)data.data);
			return RET_ERROR_WRONG_MD5;
		} else
			return RET_OK;
	} else {
		char *origfilename;
		/* copy file in and calculate it's md5sum */
		origfilename = calc_dirconcat(sourcedir,basename);
		if( origfilename == NULL )
			return RET_ERROR_OOM;

		//TODO: replace thiswith a copyfile, that checks for the md5sum...
		r = copyfile_md5known(db->mirrordir,filekey,origfilename,md5sum);
		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr,"Error copying file %s to %s/%s\n",
					origfilename,db->mirrordir,filekey);
			free(origfilename);
			return r;
		}
		free(origfilename);

		r = files_add(db,filekey,md5sum);
		return r;
	}
}

/* Make sure filekeys with md5sums are in the pool. If not take from
 * sourcedir/file where file is the entry from files */
retvalue files_checkinfiles(filesdb db,const char *sourcedir,
		const struct strlist *basefilenames,
		const struct strlist *filekeys,
		const struct strlist *md5sums) {

	retvalue result,r;
	int i;

	assert( sourcedir && basefilenames && filekeys && md5sums);
	assert( basefilenames->count == filekeys->count && filekeys->count == md5sums->count );

	result = RET_NOTHING;
	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *basename = basefilenames->values[i];	
		const char *filekey = filekeys->values[i];	
		const char *md5sum = md5sums->values[i];	

		r = files_checkinfile(db,sourcedir,basename,filekey,md5sum);
		RET_UPDATE(result,r);
		
	}
	return result;
}
