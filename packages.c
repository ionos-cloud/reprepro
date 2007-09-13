/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <zlib.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "md5sum.h"
#include "dirs.h"
#include "files.h"
#include "database_p.h"
#include "packages.h"

struct s_packagesdb {
	char *identifier;
	DB *database;
};

extern int verbose;

// to make sure the problems do not arise there:
int isopen = 0;

/* release the packages-database initialized got be packages_initialize */
retvalue packages_done(packagesdb db) {
	int r;

	isopen--;
	if( isopen < 0 )
		fprintf(stderr,"isopen: %d\n",isopen);

	r = db->database->close(db->database,0);
	free(db->identifier);
	free(db);
	if( r < 0 )
		return RET_DBERR(r);
	else
		return RET_OK;
}

retvalue packages_init(packagesdb *db,struct database *database,const char *codename,const char *component,const char *architecture,const char *packagetype) {
	char * identifier;
	retvalue r;

	identifier = calc_identifier(codename,component,architecture,packagetype);
	if( identifier == NULL )
		return RET_ERROR_OOM;

	r = packages_initialize(db, database, identifier);
	free(identifier);
	return r;
}

/* initialize the packages-database for <identifier> */
retvalue packages_initialize(packagesdb *db,struct database *database,const char *identifier) {
	packagesdb pkgs;
	retvalue r;

	assert( !database->nopackages );
	if( database->nopackages ) {
		fputs("Internal Error: Accessing packages database while that was not prepared!\n", stderr);
		return RET_ERROR;
	}

	pkgs = malloc(sizeof(struct s_packagesdb));
	if( pkgs == NULL ) {
		return RET_ERROR_OOM;
	}
	pkgs->identifier = strdup(identifier);
	if( pkgs->identifier == NULL ) {
		free(pkgs);
		return RET_ERROR_OOM;
	}
	isopen++;
	if( isopen > 1 )
		fprintf(stderr,"isopen: %d\n",isopen);

	// TODO: allow users of this function to specify readonly
	r = database_opentable(database, "packages.db", identifier,
			DB_BTREE, 0 , NULL, false, &pkgs->database);
	if( RET_WAS_ERROR(r) ) {
		free(pkgs->identifier);
		free(pkgs);
		return r;
	}
	*db = pkgs;
	return RET_OK;
}

/* replace a save chunk with another */
retvalue packages_replace(packagesdb db,const char *package,const char *chunk) {
	int dbret;
	DBT key,data;

	SETDBT(key,package);
	if ((dbret = db->database->del(db->database, NULL, &key, 0)) == 0) {
		if( verbose > 2 )
			printf("db: removed old '%s' from '%s'.\n", (const char *)key.data,db->identifier);
	} else {
		db->database->err(db->database, dbret, "packages.db, while removing old %s:",package);
		if( dbret != DB_NOTFOUND )
			return RET_DBERR(dbret);
	}
	SETDBT(key,package);
	SETDBT(data,chunk);
	if ((dbret = db->database->put(db->database, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose > 2 )
			printf("db: '%s' added to '%s'.\n", (const char *)key.data,db->identifier);
		return RET_OK;
	} else {
		db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		return RET_DBERR(dbret);
	}
}

/* save a given chunk in the database */
retvalue packages_add(packagesdb db,const char *package,const char *chunk) {
	int dbret;
	DBT key,data;

	SETDBT(key,package);
	SETDBT(data,chunk);
	if ((dbret = db->database->put(db->database, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose > 2 )
			printf("db: '%s' added to '%s'.\n", (const char *)key.data,db->identifier);
		return RET_OK;
	} else {
		db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		return RET_DBERR(dbret);
	}
}

/* get the saved chunk from the database */
retvalue packages_get(packagesdb db,const char *package,char **chunk) {
	int dbret;
	DBT key,data;

	SETDBT(key,package);
	CLEARDBT(data);

	if( (dbret = db->database->get(db->database, NULL, &key, &data, 0)) == 0){
		char *c;
		c = strdup(data.data);
		if( c == NULL )
			return RET_ERROR_OOM;
		else {
			*chunk = c;
			return RET_OK;
		}
	} else if( dbret == DB_NOTFOUND ){
		return RET_NOTHING;
	} else {
		 db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		 return RET_DBERR(dbret);
	}
}

/* remove a given chunk from the database */
retvalue packages_remove(packagesdb db,const char *package) {
	int dbret;
	DBT key;

	SETDBT(key,package);
	if ((dbret = db->database->del(db->database, NULL, &key, 0)) == 0) {
		if( verbose > 2 )
			printf("db: '%s' removed from '%s'.\n", (const char *)key.data,db->identifier);
		return RET_OK;
	} else {
		db->database->err(db->database, dbret, "packages.db:");
		return RET_DBERR(dbret);
	}
}

/* check for existance of the given version of a package in the arch,
static retvalue package_check(packagesdb db,const char *package) {
	int dbret;
	DBT key,data;

	SETDBT(key,package);
	CLEARDBT(data);

	if( (dbret = db->database->get(db->database, NULL, &key, &data, 0)) == 0){
		return RET_OK;
	} else if( dbret == DB_NOTFOUND ){
		return RET_NOTHING;
	} else {
		 db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		 return RET_DBERR(dbret);
	}
}
*/

/* action to be called by packages_forall */
//typedef retvalue per_package_action(void *data,const char *package,const char *chunk);

/* call action once for each saved chunk: */
retvalue packages_foreach(packagesdb db,per_package_action action,void *privdata) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue ret,r;

	cursor = NULL;
	if( (dbret = db->database->cursor(db->database,NULL,&cursor,0)) != 0 ) {
		db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		return RET_ERROR;
	}
	CLEARDBT(key);
	CLEARDBT(data);

	ret = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		r = action(privdata,(const char*)key.data,(const char*)data.data);
		RET_UPDATE(ret,r);
		if( RET_WAS_ERROR(r) ) {
			if( verbose > 0 )
				fprintf(stderr,"packages_foreach: Stopping procession of further packages due to previous errors\n");
			break;
		}
		CLEARDBT(key);
		CLEARDBT(data);
	}

	if( dbret != 0 && dbret != DB_NOTFOUND ) {
		db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		return RET_DBERR(dbret);
	}

	return ret;
}

/* action to be called by packages_processall */
//typedef retvalue per_package_modifier(void *data,const char *package,const char *chunk, char **newchunk);

/* call action once for each saved chunk and replace with a new one, if it returns RET_OK: */
retvalue packages_modifyall(packagesdb db, per_package_modifier *action, const struct distribution *privdata, bool *setifmodified) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result,r;

	cursor = NULL;
	if( (dbret = db->database->cursor(db->database,NULL,&cursor,0/*DB_WRITECURSOR*/)) != 0 ) {
		db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		return RET_ERROR;
	}
	CLEARDBT(key);
	CLEARDBT(data);

	result = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		char *newdata = NULL;
		r = action(privdata,(const char*)key.data,(const char*)data.data,&newdata);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) ) {
			if( verbose > 0 )
				fprintf(stderr,"packages_modifyall: Stopping procession of further packages due to privious errors\n");
			break;
		}
		if( RET_IS_OK(r) ) {
			SETDBT(data,newdata);
			dbret = cursor->c_put(cursor,&key,&data,DB_CURRENT);
			free(newdata);
			if( dbret != 0 ) {
				db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
				return RET_DBERR(dbret);
			}
			if( setifmodified != NULL )
				*setifmodified = true;
		}
		CLEARDBT(key);
		CLEARDBT(data);
	}

	if( dbret != 0 && dbret != DB_NOTFOUND ) {
		db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		db->database->err(db->database, dbret, "packages.db(%s):",db->identifier);
		return RET_DBERR(dbret);
	}

	return result;
}

/* Get a list of all identifiers having a package list */
retvalue packages_getdatabases(struct database *database, struct strlist *identifiers) {

	return database_listsubtables(database, "packages.db", identifiers);
}

/* drop a database */
retvalue packages_drop(struct database *database, const char *identifier) {

	return database_dropsubtable(database, "packages.db", identifier);
}
