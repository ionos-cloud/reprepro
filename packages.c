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
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <db.h>
#include <zlib.h>
#include "error.h"
#include "md5sum.h"
#include "dirs.h"
#include "packages.h"

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

extern int verbose;

/* release the packages-database initialized got be packages_initialize */
int packages_done(DB *db) {
	/* just in case we want something here later */
	return db->close(db,0);
}

/* initialize the packages-database for <identifier> */
DB *packages_initialize(const char *dbpath,const char *identifier) {
	DB *dbp;
	int dbret;
	char *filename;

	
	asprintf(&filename,"%s/packages.db",dbpath);
	if( make_parent_dirs(filename) < 0 ) {
		free(filename);
		return NULL;
	}
	if ((dbret = db_create(&dbp, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(dbret));
		free(filename);
		return NULL;
	}
	if ((dbret = dbp->open(dbp, filename, identifier, DB_BTREE, DB_CREATE, 0664)) != 0) {
		dbp->err(dbp, dbret, "%s", filename);
		dbp->close(dbp,0);
		free(filename);
		return NULL;
	}                     
	free(filename);
	return dbp;
}

/* replace a save chunk with another */
int packages_replace(DB *packagesdb,const char *package,const char *chunk) {
	int dbret;
	DBT key,data;

	SETDBT(key,package);
	if ((dbret = packagesdb->del(packagesdb, NULL, &key, 0)) == 0) {
		if( verbose > 2 )
			printf("db: %s: old package forgotten.\n", (const char *)key.data);
	} else {
		packagesdb->err(packagesdb, dbret, "packages.db, while removing old %s:",package);
		if( dbret != DB_NOTFOUND )
			return dbret;
	}
	SETDBT(key,package);
	SETDBT(data,chunk);
	if ((dbret = packagesdb->put(packagesdb, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose > 2 )
			printf("db: %s: package-chunk added.\n", (const char *)key.data);
	} else {
		packagesdb->err(packagesdb, dbret, "packages.db:");
	}
	return dbret;
}

/* save a given chunk in the database */
int packages_add(DB *packagesdb,const char *package,const char *chunk) {
	int dbret;
	DBT key,data;

	SETDBT(key,package);
	SETDBT(data,chunk);
	if ((dbret = packagesdb->put(packagesdb, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose > 2 )
			printf("db: %s: package-chunk added.\n", (const char *)key.data);
	} else {
		packagesdb->err(packagesdb, dbret, "packages.db:");
	}
	return dbret;
}

/* get the saved chunk from the database */
char *packages_get(DB *packagesdb,const char *package) {
	int dbret;
	DBT key,data;

	SETDBT(key,package);
	CLEARDBT(data);

	if( (dbret = packagesdb->get(packagesdb, NULL, &key, &data, 0)) == 0){
		return strdup(data.data);
	} else if( dbret == DB_NOTFOUND ){
		return NULL;
	} else {
		 packagesdb->err(packagesdb, dbret, "packages.db:");
		 return NULL;
	}
}

/* remove a given chunk from the database */
int packages_remove(DB *packagesdb,const char *package) {
	int dbret;
	DBT key;

	SETDBT(key,package);
	if ((dbret = packagesdb->del(packagesdb, NULL, &key, 0)) == 0) {
		if( verbose > 2 )
			printf("db: %s: package forgotten.\n", (const char *)key.data);
	} else {
		packagesdb->err(packagesdb, dbret, "packages.db:");
	}
	return dbret;
}

/* check for existance of the given version of a package in the arch, */
retvalue package_check(DB *packagesdb,const char *package) {
	int dbret;
	DBT key,data;

	SETDBT(key,package);
	CLEARDBT(data);

	if( (dbret = packagesdb->get(packagesdb, NULL, &key, &data, 0)) == 0){
		return RET_OK;
	} else if( dbret == DB_NOTFOUND ){
		return RET_NOTHING;
	} else {
		 packagesdb->err(packagesdb, dbret, "packages.db:");
		 return RET_DBERR(dbret);
	}
}

/* action to be called by packages_forall */
//typedef int per_package_action(void *data,const char *package,const char *chunk);

/* call action once for each saved chunk: */
retvalue packages_foreach(DB *packagesdb,per_package_action action,void *privdata) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue ret,r;

	cursor = NULL;
	if( (dbret = packagesdb->cursor(packagesdb,NULL,&cursor,0)) != 0 ) {
		packagesdb->err(packagesdb, dbret, "packages.db:");
		return -1;
	}
	CLEARDBT(key);	
	CLEARDBT(data);	

	ret = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		r = action(privdata,(const char*)key.data,(const char*)data.data);
		RET_UPDATE(ret,r);
		if( r < 0 ) 
			break;
	}

	if( dbret != 0 && dbret != DB_NOTFOUND ) {
		packagesdb->err(packagesdb, dbret, "packages.db:");
		return RET_ERROR;
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		packagesdb->err(packagesdb, dbret, "packages.db:");
		return RET_ERROR;
	}

	return ret;
}


static retvalue printout(void *data,const char *package,const char *chunk) {
	FILE *pf = data;

	fwrite(chunk,strlen(chunk),1,pf);
	fwrite("\n",1,1,pf);
	return RET_OK;
}

static retvalue zprintout(void *data,const char *package,const char *chunk) {
	gzFile pf = data;

	gzwrite(pf,(const voidp)chunk,strlen(chunk));
	gzwrite(pf,"\n",1);
	return RET_OK;
}

/* print the database to a "Packages" or "Sources" file */
retvalue packages_printout(DB *packagesdb,const char *filename) {
	retvalue ret;
	int r;
	FILE *pf;

	pf = fopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		return RET_ERRNO(errno);
	}
	ret = packages_foreach(packagesdb,printout,pf);
	r = fclose(pf);
	if( r != 0 )
		RET_UPDATE(ret,RET_ERRNO(errno));
	return ret;
}

/* print the database to a "Packages.gz" or "Sources.gz" file */
retvalue packages_zprintout(DB *packagesdb,const char *filename) {
	retvalue ret;
	int r;
	gzFile pf;

	pf = gzopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		/* if errno is zero, it's a memory error: */
		return RET_ERRNO(errno);
	}
	ret = packages_foreach(packagesdb,zprintout,pf);
	r = gzclose(pf);
	if( r < 0 )
		RET_UPDATE(ret,RET_ZERRNO(r));
	return ret;
}

