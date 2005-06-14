/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <zlib.h>
#include <db.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "md5sum.h"
#include "dirs.h"
#include "reference.h"
#include "files.h"
#include "target.h"
#include "packages.h"
#include "tracking.h"

struct s_packagesdb {
	char *identifier;
	DB *database;
};

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

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

retvalue packages_init(packagesdb *db,const char *dbpath,const char *codename,const char *component,const char *architecture,const char *packagetype) {
	char * identifier;
	retvalue r;

	identifier = calc_identifier(codename,component,architecture,packagetype);
	if( identifier == NULL )
		return RET_ERROR_OOM;

	r = packages_initialize(db,dbpath,identifier);
	free(identifier);
	return r;
}

/* initialize the packages-database for <identifier> */
retvalue packages_initialize(packagesdb *db,const char *dbpath,const char *identifier) {
	packagesdb pkgs;
	char *filename;
	int dbret;
	retvalue r;

	filename=calc_dirconcat(dbpath,"packages.db");
	if( filename == NULL )
		return RET_ERROR_OOM;
	r = dirs_make_parent(filename);
	if( RET_WAS_ERROR(r) ) {
		free(filename);
		return r;
	}
	pkgs = malloc(sizeof(struct s_packagesdb));
	if( pkgs == NULL ) {
		free(filename);
		return RET_ERROR_OOM;
	}
	pkgs->identifier = strdup(identifier);
	if( pkgs->identifier == NULL ) {
		free(filename);
		free(pkgs);
		return RET_ERROR_OOM;
	}
	
	if ((dbret = db_create(&pkgs->database, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s:%s %s\n", filename,identifier,db_strerror(dbret));
		free(filename);
		free(pkgs->identifier);
		free(pkgs);
		return RET_DBERR(dbret);
	}
	isopen++;
	if( isopen > 1 )
		fprintf(stderr,"isopen: %d\n",isopen);
	if ((dbret = pkgs->database->open(pkgs->database, filename, identifier, DB_BTREE, DB_CREATE, 0664)) != 0) {
		pkgs->database->err(pkgs->database, dbret, "%s(%s)", filename,identifier);
		(void)pkgs->database->close(pkgs->database,0);
		free(filename);
		free(pkgs->identifier);
		free(pkgs);
		return RET_DBERR(dbret);
	}                     
	free(filename);
	*db = pkgs;
	return RET_OK;
}

/* replace a save chunk with another */
static retvalue packages_replace(packagesdb db,const char *package,const char *chunk) {
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
static retvalue packages_add(packagesdb db,const char *package,const char *chunk) {
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

/* check for existance of the given version of a package in the arch, */
retvalue package_check(packagesdb db,const char *package) {
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

/* action to be called by packages_forall */
//typedef retvalue per_package_action(void *data,const char *package,const char *chunk);

/* call action once for each saved chunk: */
retvalue packages_foreach(packagesdb db,per_package_action action,void *privdata, int force) {
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
		if( RET_WAS_ERROR(r) && force <= 0 ) {
			if( verbose > 0 )
				fprintf(stderr,"packages_foreach: Stopping procession of further packages due to privious errors\n");
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
retvalue packages_modifyall(packagesdb db,per_package_modifier *action,void *privdata,bool_t *setifmodified) {
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
				*setifmodified = TRUE;
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

retvalue packages_insert(references refs, packagesdb packagesdb,
		const char *packagename, const char *controlchunk,
		const struct strlist *files,
		struct strlist *oldfiles,
		struct strlist *dereferencedfilekeys,
		struct trackingdata *trackingdata,
		enum filetype filetype,
		/*@only@*/char *oldsource,/*@only@*/char *oldsversion) {
		

	retvalue result,r;

	/* mark it as needed by this distribution */

	r = references_insert(refs,packagesdb->identifier,files,oldfiles);

	if( RET_WAS_ERROR(r) ) {
		if( oldfiles != NULL )
			strlist_done(oldfiles);
		return r;
	}

	/* Add package to the distribution's database */

	if( oldfiles != NULL ) {
		result = packages_replace(packagesdb,packagename,controlchunk);

	} else {
		result = packages_add(packagesdb,packagename,controlchunk);
	}

	if( RET_WAS_ERROR(result) ) {
		if( oldfiles != NULL )
			strlist_done(oldfiles);
		return result;
	}

	r = trackingdata_insert(trackingdata,filetype,files,oldsource,oldsversion,oldfiles,refs);
	RET_UPDATE(result,r);

	/* remove old references to files */

	if( oldfiles != NULL ) {
		r = references_delete(refs,packagesdb->identifier,
				oldfiles,files,dereferencedfilekeys);
		RET_UPDATE(result,r);
	}

	return result;
}

