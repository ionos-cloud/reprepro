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
	int ret;
	char *filename;

	
	asprintf(&filename,"%s/packages.db",dbpath);
	if( make_parent_dirs(filename) < 0 ) {
		free(filename);
		return NULL;
	}
	if ((ret = db_create(&dbp, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		free(filename);
		return NULL;
	}
	if ((ret = dbp->open(dbp, filename, identifier, DB_BTREE, DB_CREATE, 0664)) != 0) {
		dbp->err(dbp, ret, "%s", filename);
		dbp->close(dbp,0);
		free(filename);
		return NULL;
	}                     
	free(filename);
	return dbp;
}

/* replace a save chunk with another */
int packages_replace(DB *packagesdb,const char *package,const char *chunk) {
	int ret;
	DBT key,data;

	SETDBT(key,package);
	if ((ret = packagesdb->del(packagesdb, NULL, &key, 0)) == 0) {
		if( verbose > 2 )
			printf("db: %s: old package forgotten.\n", (const char *)key.data);
	} else {
		packagesdb->err(packagesdb, ret, "packages.db, while removing old %s:",package);
		if( ret != DB_NOTFOUND )
			return ret;
	}
	SETDBT(key,package);
	SETDBT(data,chunk);
	if ((ret = packagesdb->put(packagesdb, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose > 2 )
			printf("db: %s: package-chunk added.\n", (const char *)key.data);
	} else {
		packagesdb->err(packagesdb, ret, "packages.db:");
	}
	return ret;
}

/* save a given chunk in the database */
int packages_add(DB *packagesdb,const char *package,const char *chunk) {
	int ret;
	DBT key,data;

	SETDBT(key,package);
	SETDBT(data,chunk);
	if ((ret = packagesdb->put(packagesdb, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		if( verbose > 2 )
			printf("db: %s: package-chunk added.\n", (const char *)key.data);
	} else {
		packagesdb->err(packagesdb, ret, "packages.db:");
	}
	return ret;
}

/* get the saved chunk from the database */
char *packages_get(DB *packagesdb,const char *package) {
	int ret;
	DBT key,data;

	SETDBT(key,package);
	CLEARDBT(data);

	if( (ret = packagesdb->get(packagesdb, NULL, &key, &data, 0)) == 0){
		return strdup(data.data);
	} else if( ret == DB_NOTFOUND ){
		return NULL;
	} else {
		 packagesdb->err(packagesdb, ret, "packages.db:");
		 return NULL;
	}
}

/* remove a given chunk from the database */
int packages_remove(DB *packagesdb,const char *package) {
	int ret;
	DBT key;

	SETDBT(key,package);
	if ((ret = packagesdb->del(packagesdb, NULL, &key, 0)) == 0) {
		if( verbose > 2 )
			printf("db: %s: package forgotten.\n", (const char *)key.data);
	} else {
		packagesdb->err(packagesdb, ret, "packages.db:");
	}
	return ret;
}

/* check for existance of the given version of a package in the arch, */
int package_check(DB *packagesdb,const char *package) {
	int ret;
	DBT key,data;

	SETDBT(key,package);
	CLEARDBT(data);

	if( (ret = packagesdb->get(packagesdb, NULL, &key, &data, 0)) == 0){
		return 1;
	} else if( ret == DB_NOTFOUND ){
		return 0;
	} else {
		 packagesdb->err(packagesdb, ret, "packages.db:");
		 return -2;
	}
}

/* action to be called by packages_forall */
//typedef int per_package_action(void *data,const char *package,const char *chunk);

/* call action once for each saved chunk: */
int packages_foreach(DB *packagesdb,per_package_action action,void *privdata) {
	DBC *cursor;
	DBT key,data;
	int ret,r;

	cursor = NULL;
	if( (ret = packagesdb->cursor(packagesdb,NULL,&cursor,0)) != 0 ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}
	CLEARDBT(key);	
	CLEARDBT(data);	


	while( (ret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		r = action(privdata,(const char*)key.data,(const char*)data.data);
		if( r < 0 ) 
			break;
	}

	if( ret != 0 && ret != DB_NOTFOUND ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}
	if( (ret = cursor->c_close(cursor)) != 0 ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}

	return 1;
}


static int printout(void *data,const char *package,const char *chunk) {
	FILE *pf = data;

	fwrite(chunk,strlen(chunk),1,pf);
	fwrite("\n",1,1,pf);
	return 1;
}

static int zprintout(void *data,const char *package,const char *chunk) {
	gzFile pf = data;

	gzwrite(pf,(const voidp)chunk,strlen(chunk));
	gzwrite(pf,"\n",1);
	return 1;
}

/* print the database to a "Packages" or "Sources" file */
int packages_printout(DB *packagesdb,const char *filename) {
	int ret;
	FILE *pf;

	pf = fopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		return -1;
	}
	ret = packages_foreach(packagesdb,printout,pf);
	fclose(pf);
	return ret;
}

/* print the database to a "Packages.gz" or "Sources.gz" file */
int packages_zprintout(DB *packagesdb,const char *filename) {
	int ret;
	gzFile pf;

	pf = gzopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		return -1;
	}
	ret = packages_foreach(packagesdb,zprintout,pf);
	gzclose(pf);
	return ret;
}

