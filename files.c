/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
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
#include "ignore.h"
#include "filelist.h"
#include "debfile.h"

struct s_filesdb {
	DB *database;
	DB *contents;
	char *mirrordir;
};

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
	if( filename == NULL ) {
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
	dbret = DB_OPEN(db->database, filename, "md5sums", DB_BTREE,DB_CREATE);
	if (dbret != 0) {
		db->database->err(db->database, dbret, "%s", filename);
		(void)db->database->close(db->database,0);
		free(filename);
		free(db->mirrordir);
		free(db);
		return RET_DBERR(dbret);
	}
	free(filename);
	filename = calc_dirconcat(dbpath,"contents.cache.db");
	if( filename == NULL ) {
		(void)db->database->close(db->database,0);
		free(db->mirrordir);
		free(db);
		return RET_ERROR_OOM;
	}
	if ((dbret = db_create(&db->contents, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(dbret));
		(void)db->database->close(db->database,0);
		free(filename);
		free(db->mirrordir);
		free(db);
		return RET_DBERR(dbret);
	}
	dbret = DB_OPEN(db->contents,filename,"filelists",DB_BTREE,DB_CREATE);
	if( dbret != 0 ) {
		db->contents->err(db->contents, dbret, "%s", filename);
		(void)db->contents->close(db->contents,0);
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
	int dberr,dberr2;

	assert( db != NULL);

	free( db->mirrordir );
	/* just in case we want something here later */
	dberr = db->database->close(db->database,0);
	dberr2 = db->contents->close(db->contents,0);
	free(db);
	if( dberr != 0 )
		return RET_DBERR(dberr);
	else if( dberr2 != 0 )
		return RET_DBERR(dberr2);
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
		if( verbose > 6 )
			printf("db: %s: file added.\n", (const char *)key.data);
		return RET_OK;
	} else {
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
}

static retvalue files_get(filesdb db,const char *filekey,/*@out@*/char **md5sum) {
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
retvalue files_remove(filesdb db,const char *filekey, bool_t ignoremissing) {
	int dbret;
	DBT key;

	if( db->contents != NULL ) {
		SETDBT(key,filekey);
		(void)db->contents->del(db->contents,
				NULL, &key, 0);
	}
	SETDBT(key,filekey);
	dbret = db->database->del(db->database, NULL, &key, 0);
	if( dbret == 0 ) {
		if( verbose > 6 )
			printf("db: %s: file forgotten.\n", (const char *)key.data);
		return RET_OK;
	} else if( dbret == DB_NOTFOUND ) {
		if( ignoremissing )
			return RET_NOTHING;
		else {
			fprintf(stderr, "To be forgotten filekey '%s' was not known.\n",
					filekey);
			return RET_ERROR_MISSING;
		}
	} else {
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
}

/* delete the file and remove its md5sum from database */
retvalue files_deleteandremove(filesdb filesdb,const char *filekey,bool_t rmdirs, bool_t ignoreifnot) {
	int err,en;
	char *filename;
	retvalue r;

	if( interrupted() )
		return RET_ERROR_INTERUPTED;
	if( verbose >= 0 )
		printf("deleting and forgetting %s\n",filekey);
	filename = calc_fullfilename(filesdb->mirrordir,filekey);
	if( filename == NULL )
		return RET_ERROR_OOM;
	err = unlink(filename);
	if( err != 0 ) {
		en = errno;
		r = RET_ERRNO(en);
		if( errno == ENOENT ) {
			if( !ignoreifnot )
				fprintf(stderr,"%s not found, forgetting anyway\n",filename);
		} else {
			fprintf(stderr,"error while unlinking %s: %m(%d)\n",filename,en);
			free(filename);
			return r;
		}
	} else if(rmdirs) {
		/* try to delete parent directories, until one gives
		 * errors (hopefully because it still contains files) */
		size_t fixedpartlen = strlen(filesdb->mirrordir);
		char *p;

		while( (p = strrchr(filename,'/')) != NULL ) {
			/* do not try to remove parts of the mirrordir */
			if( (size_t)(p-filename) <= fixedpartlen+1 )
				break;
			*p ='\0';
			/* try to rmdir the directory, this will
			 * fail if there are still other files or directories
			 * in it: */
			err = rmdir(filename);
			if( err == 0 ) {
				if( verbose >= 1 ) {
					printf("removed now empty directory %s\n",filename);
				}
			} else {
				en = errno;
				if( en != ENOTEMPTY ) {
					//TODO: check here if only some
					//other error was first and it
					//is not empty so we do not have
					//to remove it anyway...
					fprintf(stderr,"ignoring error trying to rmdir %s: %m(%d)\n",filename,en);
				}
				/* parent directories will contain this one
				 * thus not be empty, in other words:
				 * everything's done */
				break;
			}
		}

	}
	free(filename);
	return files_remove(filesdb, filekey, ignoreifnot);
}

static retvalue files_calcmd5(/*@out@*/char **md5sum,const char *filename) {
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
	retvalue ret;

	filename = calc_fullfilename(filesdb->mirrordir,filekey);

	if( filename == NULL )
		return RET_ERROR_OOM;

	ret = md5sum_ensure(filename,md5sum,TRUE);
	free(filename);
	return ret;
}

/* check if file is already there (RET_NOTHING) or could be added (RET_OK)
 * or RET_ERROR_WRONG_MD5SUM if filekey is already there with different md5sum */
retvalue files_ready(filesdb db,const char *filekey,const char *md5sum) {
	int dbret;
	DBT key,data;

	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (dbret = db->database->get(db->database, NULL, &key, &data, 0)) == 0){
		if( strcmp(md5sum,data.data) != 0 ) {
			fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(expect: '%s', database:'%s')!\n",filekey,md5sum,(char*)data.data);
			return RET_ERROR_WRONG_MD5;
		}
		return RET_NOTHING;
	} else if( dbret != DB_NOTFOUND ){
		 db->database->err(db->database, dbret, "files.db:");
		 return RET_DBERR(dbret);
	}
	return RET_OK;;
}

/* hardlink file with known md5sum and add it to database */
retvalue files_hardlink(filesdb db,const char *tempfile, const char *filekey,const char *md5sum) {
	retvalue ret;
	int dbret;
	DBT key,data;

	SETDBT(key,filekey);
	CLEARDBT(data);

	/* an additional check to make sure nothing tricks us into
	 * overwriting it by another file */

	if( (dbret = db->database->get(db->database, NULL, &key, &data, 0)) == 0){
		if( strcmp(md5sum,data.data) != 0 ) {
			fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(expect: '%s', database:'%s')!\n",filekey,md5sum,(char*)data.data);
			return RET_ERROR_WRONG_MD5;
		}
		return RET_NOTHING;
	} else if( dbret != DB_NOTFOUND ){
		 db->database->err(db->database, dbret, "files.db:");
		 return RET_DBERR(dbret);
	}

	ret = copyfile_hardlink(db->mirrordir, filekey, tempfile, md5sum);
	if( RET_WAS_ERROR(ret) )
		return ret;

	return files_add(db,filekey,md5sum);
}

/* check for file in the database and if not found there, if it can be detected */
retvalue files_expect(filesdb db,const char *filekey,const char *md5sum) {
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
			return RET_ERROR_MISSING;
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
retvalue files_printmd5sums(filesdb db) {
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
		size_t fk_len = key.size-1;
		const char *filekey = key.data;
		size_t md_len = data.size-1;
		const char *md5sum = data.data;

		if( filekey[fk_len] != '\0' ) {
			fprintf(stderr, "Incoherent data in file database!\n");
			cursor->c_close(cursor);
			return RET_ERROR;
		}
		if( md5sum[md_len] != '\0' ) {
			fprintf(stderr, "Incoherent data in file database!\n");
			cursor->c_close(cursor);
			return RET_ERROR;
		}
		r = action(privdata,filekey,md5sum);
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

struct checkfiledata { /*@temp@*/filesdb filesdb ; bool_t fast ; };

static retvalue getfilesize(/*@out@*/off_t *s,const char *md5sum) {
	const char *p;

	p = md5sum;
	while( *p != '\0' && !xisspace(*p) ) {
		p++;
	}
	if( *p != '\0' ) {
		while( *p != '\0' && xisspace(*p) )
			p++;
		if( *p != '\0' ) {
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

retvalue files_checkpool(filesdb filesdb,bool_t fast) {
	struct checkfiledata d;
	d.fast = fast;
	d.filesdb = filesdb;
	return files_foreach(filesdb,checkfile,&d);
}

retvalue files_detect(filesdb db,const char *filekey) {
	char *md5sum;
	char *fullfilename;
	retvalue r;
	
	fullfilename = calc_fullfilename(db->mirrordir,filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	r = md5sum_read(fullfilename,&md5sum);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Error opening '%s'!\n",fullfilename);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		free(fullfilename);
		return r;
	}
	if( verbose > 20 ) {
		fprintf(stderr,"Md5sum of '%s' is '%s'.\n",fullfilename,md5sum);
	}
	free(fullfilename);
	r = files_add(db,filekey,md5sum);
	free(md5sum);
	return r;


}

/* Include a given file into the pool. */
retvalue files_include(filesdb db,const char *sourcefilename,const char *filekey, const char *md5sum, char **calculatedmd5sum, int delete) {
	retvalue r;
	char *md5sumfound;

	if( md5sum != NULL ) {
		r = files_expect(db,filekey,md5sum);
		if( RET_WAS_ERROR(r) )
			return r;
		if( RET_IS_OK(r) ) {
			if( delete >= D_MOVE ) {
				copyfile_delete(sourcefilename);
			}
			if( calculatedmd5sum != NULL ) {
				char *n = strdup(md5sum);
				if( n == NULL )
					return RET_ERROR_OOM;
				*calculatedmd5sum = n;
			}
			return RET_OK;
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
				return RET_OK;
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
	if( sourcefilename == NULL ) {
		fprintf(stderr,"Unable to find %s/%s!\n",db->mirrordir,filekey);
		if( delete == D_INPLACE ) {
			fprintf(stderr,"Perhaps you forgot to give dpkg-buildpackage the -sa option,\n or you cound try --ignore=missingfile\n");
		}
		return RET_ERROR_MISSING;
	} if( delete == D_INPLACE ) {
		if( IGNORING("Looking around if it is elsewhere", "To look around harder, ",missingfile,"Unable to find %s/%s!\n",db->mirrordir,filekey))
			r = copyfile_copy(db->mirrordir,filekey,sourcefilename,md5sum,&md5sumfound);
		else
			r = RET_ERROR_MISSING;
		if( RET_WAS_ERROR(r) )
			return r;
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

	if( sourcedir == NULL ) {
		assert(delete == D_INPLACE);
		sourcefilename = NULL;
	} else {
		sourcefilename = calc_dirconcat(sourcedir,basename);
		if( sourcefilename == NULL )
			return RET_ERROR_OOM;
	}
	r = files_include(db,sourcefilename,filekey,md5sum,calculatedmd5sum,delete);
	free(sourcefilename);
	return r;
	
}

/* the same, but with multiple files */
retvalue files_includefiles(filesdb db,const char *sourcedir,const struct strlist *basefilenames, const struct strlist *filekeys, const struct strlist *md5sums, int delete) {

	retvalue result,r;
	int i;

	assert( sourcedir != NULL || delete == D_INPLACE );
	assert( basefilenames != NULL );
	assert( filekeys != NULL ); assert( md5sums != NULL );
	assert( basefilenames->count == filekeys->count );
	assert( filekeys->count == md5sums->count );

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

/* concat mirrordir. return NULL if OutOfMemory */
char *files_calcfullfilename(const filesdb filesdb,const char *filekey) {
	return calc_dirconcat(filesdb->mirrordir,filekey);
}

retvalue files_addfilelist(filesdb db,const char *filekey,const char *filelist) {
	int dbret;
	DBT key,data;
	const char *e;

	SETDBT(key,filekey);
	memset(&data,0,sizeof(data));
	e = filelist;
	while( *e != '\0' ) {
		while( *e != '\0' )
			e++;
		e++;
	}
	e++;
	data.data = (void*)filelist;
	data.size = e-filelist;
	dbret = db->contents->put(db->contents, NULL, &key, &data, 0);
	if( dbret == 0) {
		return RET_OK;
	} else {
		db->contents->err(db->contents, dbret, "filelists.db:");
		return RET_DBERR(dbret);
	}
}

retvalue files_getfilelist(filesdb db,const char *filekey,const struct filelist_package *package, struct filelist_list *filelist) {
	int dbret;
	DBT key,data;

	SETDBT(key,filekey);
	CLEARDBT(data);

	if( (dbret = db->contents->get(db->contents, NULL, &key, &data, 0)) == 0){
		const char *p = data.data; size_t size = data.size;

		while( size > 0 && *p != '\0' ) {
			size_t len = strnlen(p,size);
			if( p[len] != '\0' ) {
				fprintf(stderr,"Corrupt filelist for %s in filelists.db!\n",
						filekey);
				return RET_ERROR;
			}
			filelist_add(filelist, package, p);
			p += len+1;
			size -= len+1;
		}
		if( size != 1 || *p != '\0' ) {
			fprintf(stderr,"Corrupt filelist for %s in filelists.db!\n",
					filekey);
			return RET_ERROR;
		}
		return RET_OK;
	} else if( dbret != DB_NOTFOUND ){
		 db->contents->err(db->contents, dbret, "filelists.db:");
		 return RET_DBERR(dbret);
	}
	return RET_NOTHING;
}

retvalue files_genfilelist(filesdb db,const char *filekey,const struct filelist_package *package, struct filelist_list *filelist) {
	char *debfilename = calc_dirconcat(db->mirrordir, filekey);
	char *contents;
	const char *p;
	retvalue result,r;

	if( debfilename == NULL ) {
		return RET_ERROR_OOM;
	}
	r = getfilelist(&contents, debfilename);
	free(debfilename);
	if( !RET_IS_OK(r) )
		return r;
	result = files_addfilelist(db, filekey, contents);
	for( p = contents; *p != '\0'; p += strlen(p)+1 ) {
		r = filelist_add(filelist, package, p);
		RET_UPDATE(result,r);
	}
	free(contents);
	return result;
}

retvalue files_regenerate_filelist(filesdb db, bool_t reread) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result,r;

	assert( db->contents != NULL );

	cursor = NULL;
	if( (dbret = db->database->cursor(db->database,NULL,&cursor,0)) != 0 ) {
		db->database->err(db->database, dbret, "files.db:");
		return RET_DBERR(dbret);
	}
	CLEARDBT(key);	
	CLEARDBT(data);	
	result = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		size_t l = key.size-1;
		const char *filekey = key.data;

		if( filekey[l] != '\0' ) {
			fprintf(stderr, "File database is in a broken shape!\n");
			cursor->c_close(cursor);
			return RET_ERROR;
		}
		if( l > 4 && strcmp(filekey+l-4,".deb") == 0 ) {
			bool_t needed;

			if( reread )
				needed = TRUE;
			else {
				DBT listkey,listdata;

				SETDBT(listkey,filekey);
				CLEARDBT(listdata);

				dbret = db->contents->get(db->contents, NULL, &listkey, &listdata, 0);
				needed = dbret != 0;
			}
			if( needed ) {
				char *debfilename;
				char *filelist;

				debfilename = calc_dirconcat(db->mirrordir, filekey);
				if( debfilename == NULL ) {
					cursor->c_close(cursor);
					return RET_ERROR_OOM;
				}

				r = getfilelist(&filelist, debfilename);
				free(debfilename);
				if( RET_IS_OK(r) ) {
					if( verbose > 0 )
						puts(filekey);
					if( verbose > 6 ) {
						const char *p = filelist;
						while( *p != '\0' ) {
							putchar(' ');
							puts(p);
							p += strlen(p)+1;
						}
					}
					r = files_addfilelist(db, filekey, filelist);
					free(filelist);
				}
				if( RET_WAS_ERROR(r) ) {
					cursor->c_close(cursor);
					return r;
				}
			}
		}
		CLEARDBT(key);	
		CLEARDBT(data);	
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
