/*  This file is part of "reprepro"
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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
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

static retvalue files_get(filesdb db,const char *filekey,char **md5sum) {
	int dbret;
	DBT key,data;

	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (dbret = db->database->get(db->database, NULL, &key, &data, 0)) == 0){
		char *n;

		n = strdup((const char *)data.data);
		if( n == NULL )
			return RET_ERROR_OOM;
		*md5sum = n;
		return RET_OK;
	} else if( dbret != DB_NOTFOUND ){
		 db->database->err(db->database, dbret, "files.db:");
		 return RET_DBERR(dbret);
	}
	return RET_NOTHING;
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

static retvalue files_calcmd5(char **md5sum,const char *filename) {
	retvalue ret;

	*md5sum = NULL;
	ret = md5sum_read(filename,md5sum);

	if( ret == RET_NOTHING ) {
		return RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(ret) ) {
		return ret;
	}
	if( verbose > 20 ) {
		fprintf(stderr,"Md5sum of '%s' is '%s'.\n",filename,*md5sum);
	}
	return ret;

}
static retvalue files_checkmd5sum(filesdb filesdb,const char *filekey,const char *md5sum) {
	char *filename;
	char *realmd5sum;
	retvalue ret;

	filename = calc_fullfilename(filesdb->mirrordir,filekey);

	if( !filename )
		return RET_ERROR_OOM;

	ret = files_calcmd5(&realmd5sum,filename);
	if( RET_IS_OK(ret) ) {
		if( strcmp(md5sum,realmd5sum) == 0 ) {
			free(realmd5sum);
			free(filename);
			return RET_OK;
		}
		fprintf(stderr,"Unknown file \"%s\" has other md5sum (%s) than expected(%s), deleting it!\n",filekey,realmd5sum,md5sum);
		free(realmd5sum);
		if( unlink(filename) == 0 ) {
			free(filename);
			return RET_NOTHING;
		}
		fprintf(stderr,"Could not delete '%s' out of the way!\n",filename);
		free(filename);
		return RET_ERROR_WRONG_MD5;
	}
	free(filename);
	if( ret == RET_ERROR_MISSING )
		ret = RET_NOTHING;
	return ret;

}


/* check for file in the database and if not found there, if it can be detected */
int files_expect(filesdb db,const char *filekey,const char *md5sum) {
	retvalue ret;
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
	} else if( dbret != DB_NOTFOUND ){
		 db->database->err(db->database, dbret, "files.db:");
		 return RET_DBERR(dbret);
	}
	/* got DB_NOTFOUND, so have to look for the file itself: */
	
	ret = files_checkmd5sum(db,filekey,md5sum);
	if( ret == RET_NOTHING || RET_WAS_ERROR(ret) )
		return ret;

	/* add found file to database */
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

struct checkfiledata { filesdb filesdb ; int fast ; };

static retvalue getfilesize(off_t *s,const char *md5sum) {
	const char *p;

	p = md5sum;
	while( *p && !isspace(*p) ) {
		p++;
	}
	if( *p ) {
		while( *p && isspace(*p) )
			p++;
		if( *p ) {
			*s = (off_t)atoll(p);
			return RET_OK;
		}
	} 
	fprintf(stderr,"Strange md5sum as missing space: '%s'\n",md5sum);
	return RET_ERROR;
}

static retvalue checkfile(void *data,const char *filekey,const char *md5sumexpected) {
	struct checkfiledata *d = data;
	char *fullfilename;
	retvalue r;

	fullfilename = calc_dirconcat(d->filesdb->mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	if( d->fast ) {
		struct stat s;
		int i;
		off_t expectedsize;

		r = getfilesize(&expectedsize,md5sumexpected);

		if( RET_IS_OK(r) ) {
			i = stat(fullfilename,&s);
			if( i < 0 ) {
				fprintf(stderr,
"Error checking status of '%s': %m\n",fullfilename);
				r = RET_ERROR_MISSING;
			} else {
				if( !S_ISREG(s.st_mode)) {
					fprintf(stderr,
"Not a regular file: '%s'\n",fullfilename);
					r = RET_ERROR;
				} else if( s.st_size != expectedsize ) {
					fprintf(stderr,
"WRONG SIZE of '%s': expected %lld(from '%s') found %lld\n",
						fullfilename,
						(long long)expectedsize,
						md5sumexpected,(long long)s.st_size);
					r = RET_ERROR;
				} else
					r = RET_OK;
			}
		}
	} else {
		char *realmd5sum;

		r = md5sum_read(fullfilename,&realmd5sum);
		if( RET_WAS_ERROR(r) ) {
		} else if( r == RET_NOTHING ) {
			fprintf(stderr,"Missing file '%s'!\n",fullfilename);
			r = RET_ERROR_MISSING;
		} else {
			if( strcmp(realmd5sum,md5sumexpected) != 0 ) {
				fprintf(stderr,"WRONG MD5SUM of '%s': found '%s' expected '%s'\n",fullfilename,realmd5sum,md5sumexpected);
				r = RET_ERROR_WRONG_MD5;
			}
			free(realmd5sum);
		}
	}

	free(fullfilename);
	return r;
}

retvalue files_checkpool(filesdb filesdb,int fast) {
	struct checkfiledata d;
	d.fast = fast;
	d.filesdb = filesdb;
	return files_foreach(filesdb,checkfile,&d);
}

/* Include a given file into the pool. */
retvalue files_include(filesdb db,const char *sourcefilename,const char *filekey, const char *md5sum, char **calculatedmd5sum, int delete) {
	retvalue r;
	char *md5sumfound;

	assert( md5sum == NULL || calculatedmd5sum == NULL );
	if( md5sum != NULL ) {
		r = files_expect(db,filekey,md5sum);
		if( RET_WAS_ERROR(r) )
			return r;
		if( RET_IS_OK(r) ) {
			if( delete >= D_MOVE ) {
				copyfile_delete(sourcefilename);
			}
			return RET_NOTHING;
		}
	} else {
		char *md5indatabase,*md5offile;

		r = files_get(db,filekey,&md5indatabase);
		if( RET_WAS_ERROR(r) ) 
			return r;
		if( RET_IS_OK(r) ) {
			if( delete == D_INPLACE ) {
				if( calculatedmd5sum != NULL )
					*calculatedmd5sum = md5indatabase;
				else
					free(md5indatabase);
				return RET_NOTHING;
			}
			r = files_calcmd5(&md5offile,sourcefilename);
			if( RET_WAS_ERROR(r) ) {
				free(md5indatabase);
				return r;
			}
			if( strcmp(md5indatabase,md5offile) != 0 ) {
				fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(file: '%s', database:'%s')!\n",filekey,md5offile,md5indatabase);
				free(md5offile);
				free(md5indatabase);
				return RET_ERROR_WRONG_MD5;
			} else {
				// The file has the md5sum we know already.
				if( delete >= D_MOVE ) {
					copyfile_delete(sourcefilename);
				}
				if( calculatedmd5sum != NULL )
					*calculatedmd5sum = md5indatabase;
				else
					free(md5indatabase);
				free(md5offile);
				return RET_NOTHING;
			}
		}
	}
	if( delete == D_INPLACE ) {
		fprintf(stderr,"Unable to find %s/%s!\n",db->mirrordir,filekey);
		return RET_ERROR_MISSING;
	} else if( delete == D_COPY ) {
		r = copyfile_copy(db->mirrordir,filekey,sourcefilename,md5sum,&md5sumfound);
		if( RET_WAS_ERROR(r) )
			return r;
	} else {
		assert( delete >= D_MOVE );
		r = copyfile_move(db->mirrordir,filekey,sourcefilename,md5sum,&md5sumfound);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	r = files_add(db,filekey,md5sumfound);
	if( RET_WAS_ERROR(r) ) {
		free(md5sumfound);
		return r;
	}
	if( calculatedmd5sum != NULL )
		*calculatedmd5sum = md5sumfound;
	else 
		free(md5sumfound);
	return RET_OK;
}

/* same as above, but use sourcedir/basename instead of sourcefilename */
retvalue files_includefile(filesdb db,const char *sourcedir,const char *basename, const char *filekey, const char *md5sum, char **calculatedmd5sum, int delete) {
	char *sourcefilename;
	retvalue r;

	sourcefilename = calc_dirconcat(sourcedir,basename);
	if( sourcefilename == NULL )
		return RET_ERROR_OOM;
	r = files_include(db,sourcefilename,filekey,md5sum,calculatedmd5sum,delete);
	free(sourcefilename);
	return r;
	
}

/* the same, but with multiple files */
retvalue files_includefiles(filesdb db,const char *sourcedir,const struct strlist *basefilenames, const struct strlist *filekeys, const struct strlist *md5sums, int delete) {

	retvalue result,r;
	int i;

	assert( sourcedir && basefilenames && filekeys && md5sums);
	assert( basefilenames->count == filekeys->count && filekeys->count == md5sums->count );

	result = RET_NOTHING;
	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *basename = basefilenames->values[i];
		const char *filekey = filekeys->values[i];
		const char *md5sum = md5sums->values[i];

		r = files_includefile(db,sourcedir,basename,filekey,md5sum,NULL,delete);
		RET_UPDATE(result,r);
		
	}
	return result;
}
