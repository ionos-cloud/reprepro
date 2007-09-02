/*  This file is part of "reprepro"
 *  Copyright (C) 2007 Bernhard R. Link
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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "globals.h"
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "database.h"
#include "dirs.h"
#include "files.h"
#include "reference.h"
#include "database_p.h"

extern int verbose;


static void database_free(struct database *db) {
	if( db == NULL )
		return;
	free(db->directory);
	free(db);
}

retvalue database_create(struct database **result, const char *dbdir) {
	struct database *n;

	n = calloc(1, sizeof(struct database));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->directory = strdup(dbdir);
	if( n->directory == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}

	*result = n;
	return RET_OK;
}

/**********************/
/* lock file handling */
/**********************/

retvalue database_lock(struct database *db, size_t waitforlock) {
	char *lockfile;
	int fd;
	retvalue r;
	size_t tries = 0;

	// TODO: create directory
	r = dirs_make_recursive(db->directory);
	if( RET_WAS_ERROR(r) )
		return r;

	lockfile = calc_dirconcat(db->directory, "lockfile");
	if( lockfile == NULL )
		return RET_ERROR_OOM;
	fd = open(lockfile,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NOCTTY,S_IRUSR|S_IWUSR);
	while( fd < 0 ) {
		int e = errno;
		if( e == EEXIST ) {
			if( tries < waitforlock && ! interrupted() ) {
				if( verbose >= 0 )
					printf("Could not aquire lock: %s already exists!\nWaiting 10 seconds before trying again.\n", lockfile);
				sleep(10);
				tries++;
				fd = open(lockfile,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NOCTTY,S_IRUSR|S_IWUSR);
				continue;

			}
			fprintf(stderr,
"The lockfile '%s' already exists, there might be another instance with the\n"
"same database dir running. To avoid locking overhead, only one process\n"
"can access the database at the same time. Only delete the lockfile if\n"
"you are sure no other version is still running!\n", lockfile);

		} else
			fprintf(stderr,"Error creating lockfile '%s': %d=%m!\n",lockfile,e);
		free(lockfile);
		return RET_ERRNO(e);
	}
	// TODO: do some more locking of this file to avoid problems
	// with the non-atomity of O_EXCL with nfs-filesystems...
	if( close(fd) != 0 ) {
		int e = errno;
		fprintf(stderr,"(Late) Error creating lockfile '%s': %d=%m!\n",lockfile,e);
		(void)unlink(lockfile);
		free(lockfile);
		return RET_ERRNO(e);
	}
	free(lockfile);
	db->locked = true;
	return RET_OK;
}

static void releaselock(struct database *db) {
	char *lockfile;

	lockfile = calc_dirconcat(db->directory, "lockfile");
	if( lockfile == NULL )
		return;
	if( unlink(lockfile) != 0 ) {
		int e = errno;
		fprintf(stderr,"Error deleting lockfile '%s': %d=%m!\n",lockfile,e);
		(void)unlink(lockfile);
	}
	free(lockfile);
	db->locked = false;
}

retvalue database_close(struct database *db) {
	retvalue result = RET_OK, r;
	if( db->references != NULL) {
		r = references_done(db->references);
		db->references = NULL;
		RET_UPDATE(result, r);
	}
	if( db->files ) {
		r = files_done(db->files);
		db->files = NULL;
		RET_UPDATE(result, r);
	}
	if( db->locked )
		releaselock(db);
	database_free(db);
	return RET_OK;
}

retvalue database_openfiles(struct database *db, const char *mirrordir) {
	retvalue r;

	assert( db->files == NULL );

	r = files_initialize(&db->files, db, mirrordir);
	if( !RET_IS_OK(r) )
		db->files = NULL;
	return r;
}

retvalue database_openreferences(struct database *db) {
	retvalue r;

	assert( db->references == NULL );

	r = references_initialize(&db->references, db);
	if( !RET_IS_OK(r) )
		db->references = NULL;
	return r;
}

const char *database_directory(struct database *database) {
	/* this has to make sure the database directory actually
	 * exists. Though locking before already does so */
	return database->directory;
}

retvalue database_opentable(struct database *database,const char *filename,const char *subtable,DBTYPE type,u_int32_t flags,u_int32_t preflags,int (*dupcompare)(DB *,const DBT *,const DBT *),DB **result) {
	char *fullfilename;
	DB *table;
	int dbret;

	fullfilename = calc_dirconcat(database_directory(database), filename);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;

	dbret = db_create(&table, NULL, 0);
	if ( dbret != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(dbret));
		free(fullfilename);
		return RET_DBERR(dbret);
	}
	if( preflags != 0 ) {
		dbret = table->set_flags(table, preflags);
		if( dbret != 0 ) {
			table->err(table, dbret, "db_set_flags(%u):",
					(unsigned int)preflags);
			(void)table->close(table, 0);
			free(fullfilename);
			return RET_DBERR(dbret);
		}
	}
	if( dupcompare != NULL ) {
		dbret = table->set_dup_compare(table, dupcompare);
		if( dbret != 0 ) {
			table->err(table, dbret, "db_set_dup_compare:");
			(void)table->close(table, 0);
			free(fullfilename);
			return RET_DBERR(dbret);
		}
	}

#if LIBDB_VERSION == 44
#define DB_OPEN(database,filename,name,type,flags) database->open(database,NULL,filename,name,type,flags,0664)
#elif LIBDB_VERSION == 43
#define DB_OPEN(database,filename,name,type,flags) database->open(database,NULL,filename,name,type,flags,0664)
#else
#if LIBDB_VERSION == 3
#define DB_OPEN(database,filename,name,type,flags) database->open(database,filename,name,type,flags,0664)
#else
#error Unexpected LIBDB_VERSION!
#endif
#endif
	dbret = DB_OPEN(table, fullfilename, subtable, type, flags);
	if (dbret != 0) {
		if( subtable != NULL )
			table->err(table, dbret, "db_open(%s:%s)",
					fullfilename, subtable);
		else
			table->err(table, dbret, "db_open(%s)", fullfilename);
		(void)table->close(table, 0);
		free(fullfilename);
		return RET_DBERR(dbret);
	}
	free(fullfilename);
	*result = table;
	return RET_OK;
}

retvalue database_listsubtables(struct database *database,const char *filename,struct strlist *result) {
	DB *table;
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue ret,r;
	struct strlist ids;

	r = database_opentable(database, filename, NULL,
			DB_UNKNOWN, DB_RDONLY, 0, NULL, &table);
	if( !RET_IS_OK(r) )
		return r;

	cursor = NULL;
	if( (dbret = table->cursor(table, NULL, &cursor, 0)) != 0 ) {
		table->err(table, dbret, "cursor(%s):", filename);
		(void)table->close(table,0);
		return RET_ERROR;
	}
	CLEARDBT(key);
	CLEARDBT(data);

	strlist_init(&ids);

	ret = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		char *identifier = strndup(key.data, key.size);
		if( identifier == NULL ) {
			(void)table->close(table, 0);
			strlist_done(&ids);
			return RET_ERROR_OOM;
		}
		r = strlist_add(&ids, identifier);
		if( RET_WAS_ERROR(r) ) {
			(void)table->close(table, 0);
			strlist_done(&ids);
			return r;
		}
		CLEARDBT(key);
		CLEARDBT(data);
	}

	if( dbret != 0 && dbret != DB_NOTFOUND ) {
		table->err(table, dbret, "c_get(%s):", filename);
		(void)table->close(table, 0);
		strlist_done(&ids);
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		table->err(table, dbret, "c_close(%s):", filename);
		(void)table->close(table, 0);
		strlist_done(&ids);
		return RET_DBERR(dbret);
	}

	dbret = table->close(table, 0);
	if( dbret != 0 ) {
		table->err(table, dbret, "close(%s):", filename);
		strlist_done(&ids);
		return RET_DBERR(dbret);
	} else {
		strlist_move(result, &ids);
		return RET_OK;
	}
}
