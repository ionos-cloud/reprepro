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
#include <db.h>

#include "globals.h"
#include "error.h"
#include "ignore.h"
#include "strlist.h"
#include "names.h"
#include "database.h"
#include "dirs.h"
#include "files.h"
#include "reference.h"
#include "tracking.h"
#include "copyfile.h"
#include "dpkgversions.h"
#include "distribution.h"
#include "database_p.h"

extern int verbose;

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define LIBDB_VERSION_STRING "bdb" TOSTRING(DB_VERSION_MAJOR) "." TOSTRING(DB_VERSION_MINOR) "." TOSTRING(DB_VERSION_PATCH)

static void database_free(struct database *db) {
	if( db == NULL )
		return;
	free(db->directory);
	free(db->mirrordir);
	free(db->version);
	free(db->lastsupportedversion);
	free(db->dbversion);
	free(db->lastsupporteddbversion);
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
	if( db->files != NULL ) {
		table_close(db->files);
		db->files = NULL;
	}
	if( db->contents != NULL ) {
		table_close(db->contents);
		db->contents = NULL;
	}
	if( db->locked )
		releaselock(db);
	database_free(db);
	return RET_OK;
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

retvalue database_opentable(struct database *database,const char *filename,const char *subtable,DBTYPE type,u_int32_t preflags,int (*dupcompare)(DB *,const DBT *,const DBT *), bool readonly, DB **result) {
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
	dbret = DB_OPEN(table, fullfilename, subtable, type,
			readonly?DB_RDONLY:DB_CREATE);
	if( dbret == ENOENT && readonly ) {
		(void)table->close(table, 0);
		free(fullfilename);
		return RET_NOTHING;
	}
	if (dbret != 0) {
		if( subtable != NULL )
			table->err(table, dbret, "db_open(%s:%s)[%d]",
					fullfilename, subtable, dbret);
		else
			table->err(table, dbret, "db_open(%s)[%d]",
					fullfilename, dbret);
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
			DB_UNKNOWN, 0, NULL, true, &table);
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
retvalue database_dropsubtable(struct database *database, const char *table, const char *subtable) {
	char *filename;
	DB *db;
	int dbret;

	filename = calc_dirconcat(database->directory, table);
	if( filename == NULL )
		return RET_ERROR_OOM;

	if ((dbret = db_create(&db, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s %s\n", filename, db_strerror(dbret));
		free(filename);
		return RET_DBERR(dbret);
	}
	dbret = db->remove(db, filename, subtable, 0);
	if( dbret == ENOENT ) {
		free(filename);
		return RET_NOTHING;
	}
	if (dbret != 0) {
		fprintf(stderr,"Error removing '%s' from %s!\n",
				subtable, filename);
		free(filename);
		return RET_DBERR(dbret);
	}

	free(filename);
	return RET_OK;
}

static inline bool targetisdefined(const char *identifier, struct distribution *distributions) {
	struct distribution *d;
	struct target *t;

	for( d = distributions ; d != NULL ; d = d->next ) {
		for( t = d->targets; t != NULL ; t = t->next ) {
			if( strcmp(t->identifier, identifier) == 0 ) {
				t->existed = true;
				return true;
			}
		}
	}
	return false;
}

static retvalue warnidentifers(struct database *db, const struct strlist *identifiers, struct distribution *distributions, bool readonly) {
	struct distribution *d;
	struct target *t;
	const char *identifier;
	retvalue r;
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
	for( d = distributions ; d != NULL ; d = d->next ) {
		// TODO: check for new architectures here and warn then...
		if( readonly )
			continue;
		for( t = d->targets; t != NULL ; t = t->next ) {
			if( t->existed )
				continue;
			/* create database now, to test it can be created
			 * early, and to know when new architectures
			 * arrive in the future. */
			r = target_initpackagesdb(t, db, READWRITE);
			if( RET_WAS_ERROR(r) )
				return r;
			r = target_closepackagesdb(t);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return RET_OK;
}

static retvalue warnunusedtracking(const struct strlist *codenames, const struct distribution *distributions) {
	const char *codename;
	const struct distribution *d;
	int i;

	for( i = 0; i < codenames->count ; i++ ) {
		codename = codenames->values[i];

		d = distributions;
		while( d != NULL && strcmp(d->codename, codename) != 0 )
			d = d->next;
		if( d != NULL && d->tracking != dt_NONE )
			continue;

		fprintf(stderr,
"Error: tracking database contains unused '%s' database.\n", codename);
		if( ignored[IGN_undefinedtracking] == 0 ) {
			if( d == NULL )
				fputs(
"This either means you removed a distribution from the distributions config\n"
"file without calling clearvanished (or at least removealltracks), you were\n"
"bitten by a bug in retrack in versions < 3.0.0, you found a new bug or your\n"
"config does not belong to this database.\n",
						stderr);
			else
				fputs(
"This either means you removed the Tracking: options from this distribution without\n"
"calling removealltracks for it, or your config does not belong to this database.\n",
						stderr);
		}
		if( IGNORABLE(undefinedtracking) ) {
			fputs("Ignoring as --ignore=undefinedtracking given.\n",
					stderr);
			ignored[IGN_undefinedtracking]++;
			continue;
		}

		fputs("To ignore use --ignore=undefinedtracking.\n", stderr);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue readline(char **result, FILE *f, const char *versionfilename) {
	char buffer[21];
	size_t l;

	if( fgets(buffer, 20, f) == NULL ) {
		int e = errno;
		if( e == 0 ) {
			fprintf(stderr, "Error reading '%s': unexpected empty file\n",
					versionfilename);
			return RET_ERROR;
		} else {
			fprintf(stderr, "Error reading '%s': %s(errno is %d)\n",
					versionfilename, strerror(e), e);
			return RET_ERRNO(e);
		}
	}
	l = strlen(buffer);
	while( l > 0 && ( buffer[l-1] == '\r' || buffer[l-1] == '\n' ) ) {
		buffer[--l] = '\0';
	}
	if( l == 0 ) {
		fprintf(stderr, "Error reading '%s': unexpcted empty line.\n",
				versionfilename);
		return RET_ERROR;
	}
	*result = strdup(buffer);
	if( *result == NULL )
		return RET_ERROR_OOM;
	return RET_OK;
}

static retvalue readversionfile(struct database *db) {
	char *versionfilename;
	FILE *f;
	retvalue r;
	int c;

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
		db->version = NULL;
		db->lastsupportedversion = NULL;
		db->dbversion = NULL;
		db->lastsupporteddbversion = NULL;
		memset(&db->capabilities, 0, sizeof(db->capabilities));
		free(versionfilename);
		return RET_NOTHING;
	}
	/* first line is the version creating this database */
	r = readline(&db->version, f, versionfilename);
	if( RET_WAS_ERROR(r) ) {
		(void)fclose(f);
		free(versionfilename);
		return r;
	}
	/* second line says which versions of reprepro will be able to cope
	 * with this database */
	r = readline(&db->lastsupportedversion, f, versionfilename);
	if( RET_WAS_ERROR(r) ) {
		(void)fclose(f);
		free(versionfilename);
		return r;
	}
	/* next line is the version of the underlying database library */
	r = readline(&db->dbversion, f, versionfilename);
	if( RET_WAS_ERROR(r) ) {
		(void)fclose(f);
		free(versionfilename);
		return r;
	}
	/* and then the minimum version of this library needed. */
	r = readline(&db->lastsupporteddbversion, f, versionfilename);
	if( RET_WAS_ERROR(r) ) {
		(void)fclose(f);
		free(versionfilename);
		return r;
	}
	(void)fclose(f);
	free(versionfilename);

	/* check for enabled capabilities in the version */

	memset(&db->capabilities, 0, sizeof(db->capabilities));
	r = dpkgversions_cmp(db->version, "3", &c);
	if( RET_WAS_ERROR(r) )
		return r;
	if( c >= 0 )
		db->capabilities.createnewtables = true;

	/* ensure we can understand it */

	r = dpkgversions_cmp(VERSION, db->lastsupportedversion, &c);
	if( RET_WAS_ERROR(r) )
		return r;
	if( c < 0 ) {
		fprintf(stderr,
"According to %s/version this database was created with an future version\n"
"and uses features this version will not be able to understand. Aborting...\n",
				db->directory);
		return RET_ERROR;
	}

	/* ensure it's a libdb database: */

	if( strncmp(db->dbversion, "bdb", 3) != 0 ) {
		fprintf(stderr,
"According to %s/version this database was created with an yet unsupported\n"
"database library. Aborting...\n",
				db->directory);
		return RET_ERROR;
	}
	if( strncmp(db->lastsupporteddbversion, "bdb", 3) != 0 ) {
		fprintf(stderr,
"According to %s/version this database was created with an yet unsupported\n"
"database library. Aborting...\n",
				db->directory);
		return RET_ERROR;
	}
	r = dpkgversions_cmp(LIBDB_VERSION_STRING, db->lastsupporteddbversion, &c);
	if( RET_WAS_ERROR(r) )
		return r;
	if( c < 0 ) {
		fprintf(stderr,
"According to %s/version this database was created with an future version\n"
"%s of libdb the version this binary is linked against cannot yet\n"
"understand. Aborting...\n",
				db->directory, db->dbversion+3);
		return RET_ERROR;
	}
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
	if( db->version == NULL )
		fputs("0\n", f);
	else {
		fputs(db->version, f);
		fputc('\n', f);
	}
	if( db->lastsupportedversion == NULL )
		fputs("0\n", f);
	else {
		fputs(db->lastsupportedversion, f);
		fputc('\n', f);
	}
	if( db->dbversion == NULL )
		fprintf(f, "bdb%d.%d.%d\n", DB_VERSION_MAJOR, DB_VERSION_MINOR, DB_VERSION_PATCH);
	else {
		fputs(db->dbversion, f);
		fputc('\n', f);
	}
	if( db->lastsupporteddbversion == NULL )
		fprintf(f, "bdb%d.%d.0\n", DB_VERSION_MAJOR, DB_VERSION_MINOR);
	else {
		fputs(db->lastsupporteddbversion, f);
		fputc('\n', f);
	}

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

	db->version = strdup(VERSION);
	if( db->version == NULL )
		return RET_ERROR_OOM;
	db->lastsupportedversion = NULL;
	db->dbversion = NULL;
	db->lastsupporteddbversion = NULL;
	memset(&db->capabilities, 0, sizeof(db->capabilities));
	db->capabilities.createnewtables = true;
	for( d = distributions ; d != NULL ; d = d->next ) {
		for( t = d->targets ; t != NULL ; t = t->next ) {
			r = target_initpackagesdb(t, db, READWRITE);
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
	char *packagesfilename, *trackingfilename;
	bool packagesfileexists, trackingfileexists;

	r = readversionfile(db);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	packagesfilename = calc_dirconcat(db->directory, "packages.db");
	if( packagesfilename == NULL )
		return RET_ERROR_OOM;
	packagesfileexists = isregularfile(packagesfilename);
	free(packagesfilename);
	trackingfilename = calc_dirconcat(db->directory, "tracking.db");
	if( trackingfilename == NULL )
		return RET_ERROR_OOM;
	trackingfileexists = isregularfile(trackingfilename);
	free(trackingfilename);

	if( !packagesfileexists && !trackingfileexists ) {
		// TODO: handle readonly, but only once packages files may no
		// longer be generated when it is active...

		// TODO: if version > 0, there should already be one...

		return createnewdatabase(db, distributions);
	}

	if( !allowunused && !fast && packagesfileexists )  {
		struct strlist identifiers;

		r = database_listpackages(db, &identifiers);
		if( RET_WAS_ERROR(r) )
			return r;
		if( RET_IS_OK(r) ) {
			r = warnidentifers(db, &identifiers,
					distributions, readonly);
			if( RET_WAS_ERROR(r) ) {
				strlist_done(&identifiers);
				return r;
			}
			strlist_done(&identifiers);
		}
	}
	if( !allowunused && !fast && trackingfileexists )  {
		struct strlist codenames;

		r = tracking_listdistributions(db, &codenames);
		if( RET_WAS_ERROR(r) )
			return r;
		if( RET_IS_OK(r) ) {
			r = warnunusedtracking(&codenames, distributions);
			if( RET_WAS_ERROR(r) ) {
				strlist_done(&codenames);
				return r;
			}
			strlist_done(&codenames);
		}
	}
	return RET_OK;
}

/* Initialize a database.
 * - if not fast, make all kind of checks for consistency (TO BE IMPLEMENTED),
 * - if readonly, do not create but return with RET_NOTHING
 * - lock database, waiting a given amount of time if already locked
 */
retvalue database_create(struct database **result, const char *dbdir, struct distribution *alldistributions, bool fast, bool nopackages, bool allowunused, bool readonly, size_t waitforlock, bool verbosedb) {
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
	n->verbose = verbosedb;

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

/********************************************************************************
 * Stuff string parts                                                           *
 ********************************************************************************/

static const char databaseerror[] = "Internal error of the underlying BerkleyDB database:\n";

/********************************************************************************
 * Stuff to handle data in tables                                               *
 ********************************************************************************
 There is nothing that connot be solved by another layer of indirection, except
 too many levels of indirection. (Source forgotten) */

struct table {
	char *name, *subname;
	DB *berkleydb;
	bool *flagreset;
	bool readonly, verbose;
};

static void table_printerror(struct table *table, int dbret, const char *action) {
	if( table->subname != NULL )
		table->berkleydb->err(table->berkleydb, dbret,
				"%sWithin %s subtable %s at %s:",
				databaseerror, table->name, table->subname,
				action);
	else
		table->berkleydb->err(table->berkleydb, dbret,
				"%sWithin %s at %s:",
				databaseerror, table->name, action);
}

retvalue table_close(struct table *table) {
	int dbret;

	if( table == NULL )
		return RET_NOTHING;
	if( table->flagreset != NULL )
		*table->flagreset = false;
	if( table->berkleydb == NULL ) {
		assert( table->readonly );
		dbret = 0;
	} else
		dbret = table->berkleydb->close(table->berkleydb, 0);
	free(table->name);
	free(table->subname);
	free(table);
	if( dbret != 0 )
		return RET_DBERR(dbret);
	else
		return RET_OK;
}

retvalue table_getrecord(struct table *table, const char *key, char **data_p) {
	int dbret;
	DBT Key, Data;

	assert( table != NULL );
	if( table->berkleydb == NULL ) {
		assert( table->readonly );
		return RET_NOTHING;
	}

	SETDBT(Key, key);
	CLEARDBT(Data);
	Data.flags = DB_DBT_MALLOC;

	dbret = table->berkleydb->get(table->berkleydb, NULL,
			&Key, &Data, 0);
	// TODO: find out what error code means out of memory...
	if( dbret == DB_NOTFOUND )
		return RET_NOTHING;
	if( dbret != 0 ) {
		table_printerror(table, dbret, "get");
		return RET_DBERR(dbret);
	}
	if( Data.data == NULL )
		return RET_ERROR_OOM;
	if( Data.size <= 0 ||
	    ((const char*)Data.data)[Data.size-1] != '\0' ) {
		if( table->subname != NULL )
			fprintf(stderr,
"Database %s(%s) returned corrupted (not null-terminated) data!",
					table->name, table->subname);
		else
			fprintf(stderr,
"Database %s returned corrupted (not null-terminated) data!",
					table->name);
		free(Data.data);
		return RET_ERROR;
	}
	*data_p = Data.data;
	return RET_OK;
}

retvalue table_gettemprecord(struct table *table, const char *key, const char **data_p, size_t *size_p) {
	int dbret;
	DBT Key, Data;

	assert( table != NULL );
	if( table->berkleydb == NULL ) {
		assert( table->readonly );
		return RET_NOTHING;
	}

	SETDBT(Key, key);
	CLEARDBT(Data);

	dbret = table->berkleydb->get(table->berkleydb, NULL,
			&Key, &Data, 0);
	// TODO: find out what error code means out of memory...
	if( dbret == DB_NOTFOUND )
		return RET_NOTHING;
	if( dbret != 0 ) {
		table_printerror(table, dbret, "get");
		return RET_DBERR(dbret);
	}
	if( Data.data == NULL )
		return RET_ERROR_OOM;
	if( Data.size <= 0 ||
	    ((const char*)Data.data)[Data.size-1] != '\0' ) {
		if( table->subname != NULL )
			fprintf(stderr,
"Database %s(%s) returned corrupted (not null-terminated) data!",
					table->name, table->subname);
		else
			fprintf(stderr,
"Database %s returned corrupted (not null-terminated) data!",
					table->name);
		return RET_ERROR;
	}
	*data_p = Data.data;
	if( size_p != NULL )
		*size_p = Data.size;
	return RET_OK;
}

bool table_recordexists(struct table *table, const char *key) {
	int dbret;
	DBT Key, Data;

	assert( table != NULL );
	if( table->berkleydb == NULL ) {
		assert( table->readonly );
		return false;
	}

	SETDBT(Key, key);
	CLEARDBT(Data);

	dbret = table->berkleydb->get(table->berkleydb, NULL,
			&Key, &Data, 0);
	if( dbret == DB_NOTFOUND )
		return false;
	if( dbret != 0 ) {
		table_printerror(table, dbret, "get");
		return false;
	}
	return true;
}

retvalue table_adduniqsizedrecord(struct table *table, const char *key, const char *data, size_t data_size, bool allowoverwrite, bool nooverwrite) {
	int dbret;
	DBT Key, Data;

	assert( table != NULL );
	assert( !table->readonly && table->berkleydb != NULL );
	assert( data_size > 0 && data[data_size-1] == '\0' );

	SETDBT(Key, key);
	SETDBTl(Data, data, data_size);
	dbret = table->berkleydb->put(table->berkleydb, NULL,
			&Key, &Data, allowoverwrite?0:DB_NOOVERWRITE);
	if( nooverwrite && dbret == DB_KEYEXIST ) {
		/* if nooverwrite is set, do nothing and ignore: */
		return RET_NOTHING;
	}
	if( dbret != 0 ) {
		table_printerror(table, dbret, "put(uniq)");
		return RET_DBERR(dbret);
	}
	if( table->verbose ) {
		if( table->subname != NULL )
			printf("db: '%s' added to %s(%s).\n",
					key, table->name, table->subname);
		else
			printf("db: '%s' added to %s.\n",
					key, table->name);
	}
	return RET_OK;
}
retvalue table_adduniqrecord(struct table *table, const char *key, const char *data) {
	return table_adduniqsizedrecord(table, key, data, strlen(data)+1,
			false, false);
}

retvalue table_deleterecord(struct table *table, const char *key, bool ignoremissing) {
	int dbret;
	DBT Key;

	assert( table != NULL );
	assert( !table->readonly && table->berkleydb != NULL );

	SETDBT(Key, key);
	dbret = table->berkleydb->del(table->berkleydb, NULL, &Key, 0);
	if( dbret != 0 ) {
		if( dbret == DB_NOTFOUND && ignoremissing )
			return RET_NOTHING;
		table_printerror(table, dbret, "del");
		if( dbret == DB_NOTFOUND )
			return RET_ERROR_MISSING;
		else
			return RET_DBERR(dbret);
	}
	if( table->verbose ) {
		if( table->subname != NULL )
			printf("db: '%s' removed from %s(%s).\n",
					key, table->name, table->subname);
		else
			printf("db: '%s' removed from %s.\n",
					key, table->name);
	}
	return RET_OK;
}

retvalue table_replacerecord(struct table *table, const char *key, const char *data) {
	retvalue r;

	r = table_deleterecord(table, key, false);
	if( r != RET_ERROR_MISSING && RET_WAS_ERROR(r) )
		return r;
	return table_adduniqrecord(table, key, data);
}

struct cursor {
	DBC *cursor;
	retvalue r;
};

retvalue table_newglobaluniqcursor(struct table *table, struct cursor **cursor_p) {
	struct cursor *cursor;
	int dbret;

	if( table->berkleydb == NULL ) {
		assert( table->readonly );
		*cursor_p = NULL;
		return RET_OK;
	}

	cursor = calloc(1, sizeof(struct cursor));
	if( cursor == NULL )
		return RET_ERROR_OOM;

	cursor->cursor = NULL;
	cursor->r = RET_OK;
	dbret = table->berkleydb->cursor(table->berkleydb, NULL,
			&cursor->cursor, 0);
	if( dbret != 0 ) {
		table_printerror(table, dbret, "cursor");
		return RET_DBERR(dbret);
	}
	*cursor_p = cursor;
	return RET_OK;
}

retvalue table_newduplicatecursor(struct table *, const char *key, struct cursor **);

retvalue cursor_close(struct table *table, struct cursor *cursor) {
	int dbret;
	retvalue r;

	if( cursor == NULL )
		return RET_OK;

	r = cursor->r;
	dbret = cursor->cursor->c_close(cursor->cursor);
	cursor->cursor = NULL;
	free(cursor);
	if( dbret != 0 ) {
		table_printerror(table, dbret, "c_close");
		RET_UPDATE(r, RET_DBERR(dbret));
	}
	return r;
}

bool cursor_nexttemp(struct table *table, struct cursor *cursor, const char **key, const char **data) {
	DBT Key, Data;
	int dbret;

	if( cursor == NULL )
		return false;

	CLEARDBT(Key);
	CLEARDBT(Data);

	dbret = cursor->cursor->c_get(cursor->cursor, &Key, &Data, DB_NEXT);
	if( dbret == DB_NOTFOUND )
		return false;

	if( dbret != 0 ) {
		table_printerror(table, dbret, "c_get(DB_NEXT)");
		cursor->r = RET_DBERR(dbret);
		return false;
	}
	if( Key.size <= 0 || Data.size <= 0 ||
	    ((const char*)Key.data)[Key.size-1] != '\0' ||
	    ((const char*)Data.data)[Data.size-1] != '\0' ) {
		if( table->subname != NULL )
			fprintf(stderr,
"Database %s(%s) returned corrupted (not null-terminated) data!",
					table->name, table->subname);
		else
			fprintf(stderr,
"Database %s returned corrupted (not null-terminated) data!",
					table->name);
		cursor->r = RET_ERROR;
		return false;
	}
	*key = Key.data;
	*data = Data.data;
	return true;
}

bool cursor_nexttempdata(struct table *table, struct cursor *cursor, const char **key, const char **data, size_t *len_p) {
	DBT Key, Data;
	int dbret;

	if( cursor == NULL )
		return false;

	CLEARDBT(Key);
	CLEARDBT(Data);

	dbret = cursor->cursor->c_get(cursor->cursor, &Key, &Data, DB_NEXT);
	if( dbret == DB_NOTFOUND )
		return false;

	if( dbret != 0 ) {
		table_printerror(table, dbret, "c_get(DB_NEXT)");
		cursor->r = RET_DBERR(dbret);
		return false;
	}
	if( Key.size <= 0 || Data.size <= 0 ||
	    ((const char*)Key.data)[Key.size-1] != '\0' ||
	    ((const char*)Data.data)[Data.size-1] != '\0' ) {
		if( table->subname != NULL )
			fprintf(stderr,
"Database %s(%s) returned corrupted (not null-terminated) data!",
					table->name, table->subname);
		else
			fprintf(stderr,
"Database %s returned corrupted (not null-terminated) data!",
					table->name);
		cursor->r = RET_ERROR;
		return false;
	}
	if( key != NULL )
		*key = Key.data;
	*data = Data.data;
	*len_p = Data.size-1;
	return true;
}

retvalue cursor_replace(struct table *table, struct cursor *cursor, const char *data) {
	DBT Key, Data;
	int dbret;

	assert( cursor != NULL );
	assert( !table->readonly );

	CLEARDBT(Key);
	SETDBT(Data, data);

	dbret = cursor->cursor->c_put(cursor->cursor, &Key, &Data, DB_CURRENT);

	if( dbret != 0 ) {
		table_printerror(table, dbret, "c_put(DB_CURRENT)");
		return RET_DBERR(dbret);
	}
	return RET_OK;
}

retvalue cursor_delete(struct table *table, struct cursor *cursor, const char *key) {
	int dbret;

	assert( cursor != NULL );
	assert( !table->readonly );

	dbret = cursor->cursor->c_del(cursor->cursor, 0);

	if( dbret != 0 ) {
		table_printerror(table, dbret, "c_del");
		return RET_DBERR(dbret);
	}
	if( table->verbose ) {
		if( table->subname != NULL )
			printf("db: '%s' removed from %s(%s).\n",
					key, table->name, table->subname);
		else
			printf("db: '%s' removed from %s.\n",
					key, table->name);
	}
	return RET_OK;
}

bool table_isempty(struct table *table) {
	DBC *cursor;
	DBT Key, Data;
	int dbret;

	dbret = table->berkleydb->cursor(table->berkleydb, NULL,
			&cursor, 0);
	if( dbret != 0 ) {
		table_printerror(table, dbret, "cursor");
		return true;
	}
	CLEARDBT(Key);
	CLEARDBT(Data);

	dbret = cursor->c_get(cursor, &Key, &Data, DB_NEXT);
	if( dbret == DB_NOTFOUND ) {
		(void)cursor->c_close(cursor);
		return true;
	}
	if( dbret != 0 ) {
		table_printerror(table, dbret, "c_get(DB_NEXT)");
		(void)cursor->c_close(cursor);
		return true;
	}
	dbret = cursor->c_close(cursor);
	if( dbret != 0 )
		table_printerror(table, dbret, "c_close");
	return false;
}

/********************************************************************************
 * Open the different types of tables with their needed flags:                  *
 ********************************************************************************/
static retvalue database_table(struct database *database, const char *filename, const char *subtable, DBTYPE type, u_int32_t preflags, int (*dupcompare)(DB *,const DBT *,const DBT *), bool readonly, /*@out@*/struct table **table_p) {
	struct table *table;
	retvalue r;

	table = calloc(1, sizeof(struct table));
	if( table == NULL )
		return RET_ERROR_OOM;
	/* TODO: is filename always an static constant? then we could drop the dup */
	table->name = strdup(filename);
	if( table->name == NULL ) {
		free(table);
		return RET_ERROR_OOM;
	}
	if( subtable != NULL ) {
		table->subname = strdup(subtable);
		if( table->subname == NULL ) {
			free(table->name);
			free(table);
			return RET_ERROR_OOM;
		}
	} else
		table->subname = NULL;
	table->readonly = readonly;
	table->verbose = database->verbose;
	r = database_opentable(database, filename, subtable, type, preflags, dupcompare, readonly, &table->berkleydb);
	if( RET_WAS_ERROR(r) ) {
		free(table->subname);
		free(table->name);
		free(table);
		return r;
	}
	if( r == RET_NOTHING ) {
		assert( readonly );
		/* sometimes we don't want a return here, when? */
		table->berkleydb = NULL;
		r = RET_OK;
	}
	*table_p = table;
	return r;
}

retvalue database_openpackages(struct database *database, const char *identifier, bool readonly, struct table **table_p) {
	struct table *table IFSTUPIDCC(=NULL);
	retvalue r;

	if( database->nopackages ) {
		fputs("Internal Error: Accessing packages databse while that was not prepared!\n", stderr);
		return RET_ERROR;
	}
	if( database->packagesdatabaseopen ) {
		fputs("Internal Error: Trying to open multiple packages databases at the same time.\nThis should normaly not happen (to avoid triggering bugs in the underlying BerkleyDB)\n", stderr);
		return RET_ERROR;
	}

	r = database_table(database, "packages.db", identifier,
			DB_BTREE, 0, NULL, readonly, &table);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;
	table->flagreset = &database->packagesdatabaseopen;
	database->packagesdatabaseopen = true;
	*table_p = table;
	return RET_OK;
}

/* Get a list of all identifiers having a package list */
retvalue database_listpackages(struct database *database, struct strlist *identifiers) {
	return database_listsubtables(database, "packages.db", identifiers);
}

/* drop a database */
retvalue database_droppackages(struct database *database, const char *identifier) {
	return database_dropsubtable(database, "packages.db", identifier);
}

retvalue database_openfiles(struct database *db, const char *mirrordir) {
	retvalue r;
	struct strlist identifiers;

	assert( db->files == NULL && db->contents == NULL );
	assert( db->mirrordir == NULL );

	r = database_listsubtables(db, "contents.cache.db", &identifiers);
	if( RET_IS_OK(r) ) {
		if( strlist_in(&identifiers, "filelists") ) {
			fprintf(stderr,
"Your %s/contents.cache.db file still contains a table of cached filelists\n"
"in the old (pre 3.0.0) format. You have to either delete that file (and loose\n"
"all caches of filelists) or run reprepro with argument translatefilelists\n"
"to translate the old caches into the new format.\n",	db->directory);
			strlist_done(&identifiers);
			return RET_ERROR;
		}
		strlist_done(&identifiers);
	}

	db->mirrordir = strdup(mirrordir);
	if( db->mirrordir == NULL )
		return RET_ERROR_OOM;
	r = database_table(db, "files.db", "md5sums",
			DB_BTREE, 0, NULL, READWRITE, &db->files);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		free(db->mirrordir);
		db->mirrordir = NULL;
		db->files = NULL;
		return r;
	}
	r = database_table(db, "contents.cache.db", "compressedfilelists",
			DB_BTREE, 0, NULL, READWRITE, &db->contents);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		(void)table_close(db->files);
		db->mirrordir = NULL;
		db->files = NULL;
		db->contents = NULL;
	}
	return r;
}

retvalue database_openreleasecache(struct database *database, const char *codename, struct table **cachedb_p) {
	retvalue r;
	char *oldcachefilename;

	/* Since 3.1.0 it's release.caches.db, before release.cache.db.
	 * The new file also contains the sha1 checksums and is extensible
	 * for more in the future. Thus if there is only the old variant,
	 * rename to the new. (So no old version by accident uses it and
	 * puts the additional sha1 data into the md5sum fields.)
	 * If both files are there, just delete both, as neither will
	 * be very current then.
	 * */

	oldcachefilename = calc_dirconcat(database_directory(database),
			"release.cache.db");
	if( oldcachefilename == NULL )
		return RET_ERROR_OOM;
	if( isregularfile(oldcachefilename) ) {
		char *newcachefilename;

		newcachefilename = calc_dirconcat(database_directory(database),
				"release.caches.db");
		if( newcachefilename == NULL ) {
			free(oldcachefilename);
			return RET_ERROR_OOM;
		}
		if( isregularfile(newcachefilename)
		    || rename(oldcachefilename, newcachefilename) != 0) {
			fprintf(stderr,
"Deleting old-style export cache file %s!\n"
"This means that all index files (even unchanged) will be rewritten the\n"
"next time parts of their distribution are changed. This should only\n"
"happen once while migration from pre-3.1.0 to later versions.\n",
					oldcachefilename);

			if( unlink(oldcachefilename) != 0 ) {
				int e = errno;
				fprintf(stderr, "Cannot delete '%s': %s!",
						oldcachefilename,
						strerror(e));
				free(oldcachefilename);
				free(newcachefilename);
				return RET_ERRNO(e);
			}
			(void)unlink(oldcachefilename);
		}
		free(newcachefilename);
	}
	free(oldcachefilename);

	r = database_table(database, "release.caches.db", codename,
			 DB_HASH, 0, NULL, READWRITE, cachedb_p);
	if( RET_IS_OK(r) )
		(*cachedb_p)->verbose = false;
	return r;
}

/* concat mirrordir. return NULL if OutOfMemory */
char *files_calcfullfilename(const struct database *database,const char *filekey) {
	return calc_dirconcat(database->mirrordir, filekey);
}

static retvalue table_copy(struct table *oldtable, struct table *newtable) {
	retvalue r;
	struct cursor *cursor;
	const char *filekey, *data;
	size_t data_len;

	r = table_newglobaluniqcursor(oldtable, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	while( cursor_nexttempdata(oldtable, cursor, &filekey,
				&data, &data_len) ) {
		r = table_adduniqsizedrecord(newtable, filekey,
				data, data_len+1, false, true);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

retvalue database_translate_filelists(struct database *database) {
	char *dbname, *tmpdbname;
	struct table *oldtable, *newtable;
	struct strlist identifiers;
	int ret;
	retvalue r, r2;

	r = database_listsubtables(database, "contents.cache.db",
			&identifiers);
	if( RET_IS_OK(r) ) {
		if( !strlist_in(&identifiers, "filelists") ) {
			fprintf(stderr,
"Your %s/contents.cache.db file does not contain an old style database!\n",
					database->directory);
			strlist_done(&identifiers);
			return RET_NOTHING;
		}
		strlist_done(&identifiers);
	}

	dbname = calc_dirconcat(database->directory, "contents.cache.db");
	if( dbname == NULL )
		return RET_ERROR_OOM;
	tmpdbname = calc_dirconcat(database->directory, "old.contents.cache.db");
	if( tmpdbname == NULL ) {
		free(dbname);
		return RET_ERROR_OOM;
	}
	ret = rename(dbname, tmpdbname);
	if( ret != 0 ) {
		int e = errno;
		fprintf(stderr, "Could not rename '%s' into '%s': %s(%d)\n",
				dbname, tmpdbname, strerror(e), e);
		return RET_ERRNO(e);
	}
	newtable = NULL;
	r = database_table(database, "contents.cache.db",
			"compressedfilelists",
			DB_BTREE, 0, NULL, READWRITE, &newtable);
	assert( r != RET_NOTHING );
	oldtable = NULL;
	if( RET_IS_OK(r) ) {
		r = database_table(database, "old.contents.cache.db", "filelists",
				DB_BTREE, 0, NULL, READONLY, &oldtable);
		if( r == RET_NOTHING ) {
			fprintf(stderr, "Could not find old-style database!\n");
			r = RET_ERROR;
		}
	}
	if( RET_IS_OK(r) ) {
		r = filelists_translate(oldtable, newtable);
		if( r == RET_NOTHING )
			r = RET_OK;
	}
	r2 = table_close(oldtable);
	RET_ENDUPDATE(r, r2);
	oldtable = NULL;
	if( RET_IS_OK(r) ) {
		/* copy the new-style database, */
		r = database_table(database, "old.contents.cache.db", "compressedfilelists",
				DB_BTREE, 0, NULL, READONLY, &oldtable);
		if( RET_IS_OK(r) ) {
			/* if there is one... */
			r = table_copy(oldtable, newtable);
			r2 = table_close(oldtable);
			RET_ENDUPDATE(r, r2);
		}
		if( r == RET_NOTHING ) {
			r = RET_OK;
		}
	}
	r2 = table_close(newtable);
	RET_ENDUPDATE(r, r2);
	if( RET_IS_OK(r) )
		unlink(tmpdbname);

	if( RET_WAS_ERROR(r) ) {
		ret = rename(tmpdbname, dbname);
		if( ret != 0 ) {
			int e = errno;
			fprintf(stderr, "Could not rename '%s' back into '%s': %s(%d)\n",
					dbname, tmpdbname, strerror(e), e);
			free(tmpdbname);
			free(dbname);
			return RET_ERRNO(e);
		}
		free(tmpdbname);
		free(dbname);
		return r;
	}
	free(tmpdbname);
	free(dbname);
	return RET_OK;
}
