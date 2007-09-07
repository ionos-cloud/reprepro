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
#include <limits.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "globals.h"
#include "error.h"
#include "ignore.h"
#include "strlist.h"
#include "names.h"
#include "database.h"
#include "dirs.h"
#include "files.h"
#include "reference.h"
#include "copyfile.h"
#include "distribution.h"
#include "database_p.h"

extern int verbose;

/* Version numbers of the database:
 *
 * 0: compatibility (means no db/version file yet)
 *    used by reprepro until before 2.3.0
 *
 * 1: since reprepro 2.3.0
 *    - all packages.db database are created
 *      (i.e. missing ones means there is a new architecture/component)
 *
 * */

#define CURRENT_VERSION 1

static void database_free(struct database *db) {
	if( db == NULL )
		return;
	free(db->directory);
	free(db);
}

/**********************/
/* lock file handling */
/**********************/

static retvalue database_lock(struct database *db, size_t waitforlock) {
	char *lockfile;
	int fd;
	retvalue r;
	size_t tries = 0;

	assert( !db->locked );
	db->dircreationdepth = 0;
	r = dir_create_needed(db->directory, &db->dircreationdepth);
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

	assert( db->locked );

	lockfile = calc_dirconcat(db->directory, "lockfile");
	if( lockfile == NULL )
		return;
	if( unlink(lockfile) != 0 ) {
		int e = errno;
		fprintf(stderr,"Error deleting lockfile '%s': %d=%m!\n",lockfile,e);
		(void)unlink(lockfile);
	}
	free(lockfile);
	dir_remove_new(db->directory, db->dircreationdepth);
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

static inline bool targetisdefined(const char *identifier, struct distribution *distributions) {
	struct distribution *d;
	struct target *t;

	for( d = distributions ; d != NULL ; d = d->next ) {
		for( t = d->targets; t != NULL ; t = t->next ) {
			if( strcmp(t->identifier, identifier) == 0 )
				return true;
		}
	}
	return false;
}

static retvalue warnusedidentifers(const struct strlist *identifiers, struct distribution *distributions) {
	const char *identifier;
	int i;

	for( i = 0; i < identifiers->count ; i++ ) {
		identifier = identifiers->values[i];

		if( targetisdefined(identifier, distributions) )
			continue;

		fprintf(stderr,
"Error: packages database contains unused '%s' database.\n", identifier);
		if( ignored[IGN_undefinedtarget] == 0 ) {
			fputs(
"This either means you removed a distribution, component or architecture from\n"
"the distributions config file without calling clearvanished, or your config\n"
"does not belong to this database.\n",
					stderr);
		}
		if( IGNORABLE(undefinedtarget) ) {
			fputs("Ignoring as --ignore=undefinedtarget given.\n",
					stderr);
			ignored[IGN_undefinedtarget]++;
			continue;
		}

		fputs("To ignore use --ignore=undefinedtarget.\n", stderr);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue readversionfile(struct database *db) {
	char *versionfilename;
	char buffer[20], *e;
	FILE *f;
	size_t l;
	long v;

	versionfilename = calc_dirconcat(db->directory, "version");
	if( versionfilename == NULL )
		return RET_ERROR_OOM;
	f = fopen(versionfilename, "r");
	if( f == NULL ) {
		int e = errno;

		if( e != ENOENT ) {
			fprintf(stderr, "Error opening '%s': %s(errno is %d)\n",
					versionfilename, strerror(e), e);
			free(versionfilename);
			return RET_ERRNO(e);
		}
		db->version = 0;
		db->compatibilityversion = 0;
		free(versionfilename);
		return RET_NOTHING;
	}

	if( fgets(buffer, 20, f) == NULL ) {
		int e = errno;
		if( e == 0 ) {
			fprintf(stderr, "Error reading '%s': unexpected empty file\n",
					versionfilename);
			(void)fclose(f);
			free(versionfilename);
			return RET_ERROR;
		} else {
			fprintf(stderr, "Error reading '%s': %s(errno is %d)\n",
					versionfilename, strerror(e), e);
			(void)fclose(f);
			free(versionfilename);
			return RET_ERRNO(e);
		}
	}
	l = strlen(buffer);
	while( l > 0 && ( buffer[l-1] == '\r' || buffer[l-1] == '\n' ) ) {
		buffer[--l] = '\0';
	}
	v = strtol(buffer, &e, 10);
	if( l == 0 || e == NULL || *e != '\0' || v < 0 || v >= INT_MAX ) {
		fprintf(stderr, "Error reading '%s': malformed first line.\n",
				versionfilename);
		(void)fclose(f);
		free(versionfilename);
		return RET_ERROR;
	}
	db->version = v;

	/* second line says which versions of reprepro will be able to cope
	 * with this database */

	if( fgets(buffer, 20, f) == NULL ) {
		int e = errno;
		if( e == 0 ) {
			fprintf(stderr, "Error reading '%s': no second line\n",
					versionfilename);
			(void)fclose(f);
			free(versionfilename);
			return RET_ERROR;
		} else {
			fprintf(stderr, "Error reading '%s': %s(errno is %d)\n",
					versionfilename, strerror(e), e);
			(void)fclose(f);
			free(versionfilename);
			return RET_ERRNO(e);
		}
	}
	l = strlen(buffer);
	while( l > 0 && ( buffer[l-1] == '\r' || buffer[l-1] == '\n' ) ) {
		buffer[--l] = '\0';
	}
	v = strtol(buffer, &e, 10);
	if( l == 0 || e == NULL || *e != '\0' || v < 0 || v >= INT_MAX ) {
		fprintf(stderr, "Error reading '%s': malformed second line.\n",
				versionfilename);
		(void)fclose(f);
		free(versionfilename);
		return RET_ERROR;
	}
	db->compatibilityversion = v;
	(void)fclose(f);
	free(versionfilename);
	return RET_OK;
}

static retvalue writeversionfile(struct database *db) {
	char *versionfilename;
	FILE *f;
	int e;

	versionfilename = calc_dirconcat(db->directory, "version");
	if( versionfilename == NULL )
		return RET_ERROR_OOM;
	f = fopen(versionfilename, "w");
	if( f == NULL ) {
		int e = errno;

		fprintf(stderr, "Error creating '%s': %s(errno is %d)\n",
					versionfilename, strerror(e), e);
		free(versionfilename);
		return RET_ERRNO(e);
	}

	fprintf(f, "%d\n%d\n", db->version, db->compatibilityversion);
	fprintf(f, "%d.%d.%d\n", DB_VERSION_MAJOR, DB_VERSION_MINOR, DB_VERSION_PATCH);
	fprintf(f, "%d.%d.0\n", DB_VERSION_MAJOR, DB_VERSION_MINOR);
	fprintf(f, "%s %s\n", PACKAGE, VERSION);

	e = ferror(f);

	if( e != 0 ) {
		fprintf(stderr, "Error writing '%s': %s(errno is %d)\n",
				versionfilename, strerror(e), e);
		(void)fclose(f);
		free(versionfilename);
		return RET_ERRNO(e);
	}
	if( fclose(f) != 0 ) {
		e = errno;
		fprintf(stderr, "Error writing '%s': %s(errno is %d)\n",
				versionfilename, strerror(e), e);
		free(versionfilename);
		return RET_ERRNO(e);
	}
	free(versionfilename);
	return RET_OK;
}

static retvalue createnewdatabase(struct database *db, struct distribution *distributions) {
	struct distribution *d;
	struct target *t;
	retvalue result = RET_NOTHING, r;

	db->version = CURRENT_VERSION;
	for( d = distributions ; d != NULL ; d = d->next ) {
		for( t = d->targets ; t != NULL ; t = t->next ) {
			r = target_initpackagesdb(t, db);
			RET_UPDATE(result, r);
			if( RET_IS_OK(r) ) {
				r = target_closepackagesdb(t);
				RET_UPDATE(result, r);
			}
		}
	}
	r = writeversionfile(db);
	RET_UPDATE(result, r);
	return result;
}

static retvalue preparepackages(struct database *db, bool fast, bool readonly, bool allowunused, struct distribution *distributions) {
	retvalue r;
	char *packagesfilename;

	r = readversionfile(db);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	packagesfilename = calc_dirconcat(db->directory, "packages.db");
	if( packagesfilename == NULL )
		return RET_ERROR_OOM;
	if( !isregularfile(packagesfilename) ) {
		free(packagesfilename);
		// TODO: handle readonly, but only once packages files may no
		// longer be generated when it is active...

		// TODO: if version > 0, there should already be one...

		return createnewdatabase(db, distributions);
	}
	free(packagesfilename);

	if( db->compatibilityversion > CURRENT_VERSION ) {
		fprintf(stderr,
"According to %s/version this database was created with an future version\n"
"and uses features this version will not be able to understand. Aborting...\n",
				db->directory);
		return RET_ERROR;
	}

	if( !allowunused && !fast )  {
		struct strlist identifiers;

		r = packages_getdatabases(db, &identifiers);
		if( RET_WAS_ERROR(r) ) {
			return r;
		}

		if( RET_IS_OK(r) ) {
			r = warnusedidentifers(&identifiers, distributions);
			if( RET_WAS_ERROR(r) ) {
				strlist_done(&identifiers);
				return r;
			}
			strlist_done(&identifiers);
		}
	}
	return RET_OK;
}

/* Initialize a database.
 * - if not fast, make all kind of checks for consistency (TO BE IMPLEMENTED),
 * - if readonly, do not create but return with RET_NOTHING
 * - lock database, waiting a given amount of time if already locked
 */
retvalue database_create(struct database **result, const char *dbdir, struct distribution *alldistributions, bool fast, bool nopackages, bool allowunused, bool readonly, size_t waitforlock) {
	struct database *n;
	retvalue r;

	if( readonly && !isdir(dbdir) ) {
		if( verbose >= 0 )
			fprintf(stderr, "Exiting without doing anything, as there is no database yet that could result in other actions.\n");
		return RET_NOTHING;
	}

	n = calloc(1, sizeof(struct database));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->directory = strdup(dbdir);
	if( n->directory == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}

	r = database_lock(n, waitforlock);
	assert( r != RET_NOTHING );
	if( !RET_IS_OK(r) ) {
		database_free(n);
		return r;
	}
	n->readonly = readonly;

	if( nopackages ) {
		n->nopackages = true;
		*result = n;
		return RET_OK;
	}

	r = preparepackages(n, fast, readonly, allowunused, alldistributions);
	if( RET_WAS_ERROR(r) ) {
		database_close(n);
		return r;
	}

	*result = n;
	return RET_OK;
}

