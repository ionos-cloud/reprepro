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
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <db.h>
#include <zlib.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "md5sum.h"
#include "dirs.h"
#include "reference.h"
#include "files.h"
#include "packages.h"



#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

extern int verbose;

/* release the packages-database initialized got be packages_initialize */
retvalue packages_done(packagesdb db) {
	int r;
	/* just in case we want something here later */
	r = db->database->close(db->database,0);
	free(db->identifier);
	free(db);
	if( r < 0 )
		return RET_DBERR(r);
	else
		return RET_OK;
}

retvalue packages_init(packagesdb *db,const char *dbpath,const char *codename,const char *component,const char *architecture) {
	char * identifier;
	retvalue r;

	identifier = calc_identifier(codename,component,architecture);
	if( ! identifier )
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
	if( !filename )
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
		return -1;
	}
	CLEARDBT(key);	
	CLEARDBT(data);	

	ret = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		r = action(privdata,(const char*)key.data,(const char*)data.data);
		RET_UPDATE(ret,r);
		if( RET_WAS_ERROR(r) && !force ) {
			if( verbose > 0 )
				fprintf(stderr,"packages_foreach: Stopping procession of further packages due to privious errors\n");
			break;
		}
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


static retvalue printout(void *data,const char *package,const char *chunk) {
	FILE *pf = data;

	if( fwrite(chunk,strlen(chunk),1,pf) != 1 || fwrite("\n",1,1,pf) != 1 )
		return RET_ERROR;
	else
		return RET_OK;
}

static retvalue zprintout(void *data,const char *package,const char *chunk) {
	gzFile pf = data;
	size_t l;

	l = strlen(chunk);
	if( gzwrite(pf,(const voidp)chunk,l) != l || gzwrite(pf,"\n",1) != 1 )
		return RET_ERROR;
	else
		return RET_OK;
}

/* print the database to a "Packages" or "Sources" file */
static retvalue packages_printout(packagesdb packagesdb,const char *filename) {
	retvalue ret;
	int r;
	FILE *pf;

	pf = fopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		return RET_ERRNO(errno);
	}
	ret = packages_foreach(packagesdb,printout,pf,0);
	r = fclose(pf);
	if( r != 0 )
		RET_ENDUPDATE(ret,RET_ERRNO(errno));
	return ret;
}

/* print the database to a "Packages.gz" or "Sources.gz" file */
static retvalue packages_zprintout(packagesdb packagesdb,const char *filename) {
	retvalue ret;
	int r;
	gzFile pf;

	pf = gzopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		/* if errno is zero, it's a memory error: */
		return RET_ERRNO(errno);
	}
	ret = packages_foreach(packagesdb,zprintout,pf,0);
	r = gzclose(pf);
	if( r < 0 )
		RET_ENDUPDATE(ret,RET_ZERRNO(r));
	return ret;
}

/* like packages_printout, but open and close database yourself */
retvalue packages_doprintout(const char *dbpath,const char *dbname,const char *filename){
	packagesdb pkgs;
	retvalue result,r;

	r = packages_initialize(&pkgs,dbpath,dbname);
	if( RET_WAS_ERROR(r) )
		return r;
	result = packages_printout(pkgs,filename);
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);
	return result;
}

/* like packages_zprintout, but open and close database yourself */
retvalue packages_dozprintout(const char *dbpath,const char *dbname,const char *filename){
	packagesdb pkgs;
	retvalue result,r;

	r = packages_initialize(&pkgs,dbpath,dbname);
	if( RET_WAS_ERROR(r) )
		return r;
	result = packages_zprintout(pkgs,filename);
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);
	return result;
}

retvalue packages_insert(DB *referencesdb, packagesdb packagesdb,
		const char *packagename, const char *controlchunk,
		const struct strlist *files,
		const struct strlist *oldfiles) {
		

	retvalue result,r;

	/* mark it as needed by this distribution */

	r = references_insert(referencesdb,packagesdb->identifier,files,oldfiles);

	if( RET_WAS_ERROR(r) )
		return r;

	/* Add package to the distribution's database */

	if( oldfiles != NULL ) {
		result = packages_replace(packagesdb,packagename,controlchunk);

	} else {
		result = packages_add(packagesdb,packagename,controlchunk);
	}

	if( RET_WAS_ERROR(result) )
		return result;

	/* remove old references to files */

	if( oldfiles != NULL ) {
		r = references_delete(referencesdb,packagesdb->identifier,
				oldfiles,files);
		RET_UPDATE(result,r);
	}

	return result;
}

/* rereference a full database */

struct data_ref { 
	packagesdb packagesdb;
	DB *referencesdb;
	extractfilekeys *extractfilekeys;
};

static retvalue rerefpkg(void *data,const char *package,const char *chunk) {
	struct data_ref *d = data;
	struct strlist filekeys;
	retvalue r;

	r = (*d->extractfilekeys)(chunk,&filekeys);
	if( verbose >= 0 && r == RET_NOTHING ) {
		fprintf(stderr,"Package does not look like expected: '%s'\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 10 ) {
		fprintf(stderr,"adding references to '%s' for '%s': ",d->packagesdb->identifier,package);
		strlist_fprint(stderr,&filekeys);
		putc('\n',stderr);
	}
	r = references_insert(d->referencesdb,d->packagesdb->identifier,&filekeys,NULL);
	strlist_done(&filekeys);
	return r;
}

retvalue packages_rereference(const char *dbdir,DB *referencesdb,extractfilekeys *extractfilekeys,const char *codename,const char *component,const char *architecture,int force) {
	retvalue result,r;
	struct data_ref refdata;
	packagesdb pkgs;

	r = packages_init(&pkgs,dbdir,codename,component,architecture);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 1 ) {
		if( verbose > 2 )
			fprintf(stderr,"Unlocking depencies of %s...\n",pkgs->identifier);
		else
			fprintf(stderr,"Rereferencing %s...\n",pkgs->identifier);
	}

	result = references_remove(referencesdb,pkgs->identifier);

	if( verbose > 2 )
		fprintf(stderr,"Referencing %s...\n",pkgs->identifier);

	refdata.referencesdb = referencesdb;
	refdata.packagesdb = pkgs;
	refdata.extractfilekeys = extractfilekeys;
	r = packages_foreach(pkgs,rerefpkg,&refdata,force);
	RET_UPDATE(result,r);
	
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	return result;
}

/* check a full database */
struct data_check { 
	packagesdb packagesdb;
	DB *referencesdb;
	filesdb filesdb;
	extractfilekeys *extractfilekeys;
};

static retvalue checkpkg(void *data,const char *package,const char *chunk) {
	struct data_check *d = data;
	struct strlist filekeys;
	retvalue r;

	r = (*d->extractfilekeys)(chunk,&filekeys);
	if( verbose >= 0 && r == RET_NOTHING ) {
		fprintf(stderr,"Package does not look like expected: '%s'\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 10 ) {
		fprintf(stderr,"checking references to '%s' for '%s': ",d->packagesdb->identifier,package);
		strlist_fprint(stderr,&filekeys);
		putc('\n',stderr);
	}
	r = references_check(d->referencesdb,d->packagesdb->identifier,&filekeys);
	// TODO check md5sums in filesdb
	strlist_done(&filekeys);
	return r;
}

retvalue packages_check(const char *dbdir,filesdb filesdb,DB *referencesdb,extractfilekeys *extractfilekeys,const char *codename,const char *component,const char *architecture,int force) {
	retvalue result,r;
	struct data_check data;
	packagesdb pkgs;

	r = packages_init(&pkgs,dbdir,codename,component,architecture);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 1 ) {
		fprintf(stderr,"Checking packages in '%s'...\n",pkgs->identifier);
	}
	data.referencesdb = referencesdb;
	data.filesdb = filesdb;
	data.packagesdb = pkgs;
	data.extractfilekeys = extractfilekeys;
	result = packages_foreach(pkgs,checkpkg,&data,force);

	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	return result;
}
