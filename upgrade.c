/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2004 Bernhard R. Link
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
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>
#include <db.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "strtupel.h"
#include "dirs.h"
#include "names.h"
#include "chunks.h"
#include "updates.h"
#include "upgrade.h"

extern int verbose;

/* The task of the code is to identify the packages to be downloaded to
 * have the mirror in the current state described by the lists identified
 * by updates.c. */

/* release the database got by upgrade */
retvalue upgrade_done(upgradedb db) {
	int r;
	/* just in case we want something here later */
	r = db->database->close(db->database,0);
	free(db);
	if( r < 0 )
		return RET_DBERR(r);
	else
		return RET_OK;
}

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}
#define SETDBTT(dbt,datatupel) {const char *my = datatupel;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strtupel_len(my)+1;}

/* Initialize a database, clear will remove all prior data */
retvalue upgrade_initialize(upgradedb *udb,const char *dbpath,const char *identifier,int clear) {
	upgradedb db;
	char *filename;
	int dbret;
	retvalue r;

	filename=calc_dirconcat(dbpath,"upgrades.db");
	if( !filename )
		return RET_ERROR_OOM;
	r = dirs_make_parent(filename);
	if( RET_WAS_ERROR(r) ) {
		free(filename);
		return r;
	}
	db = malloc(sizeof(struct s_upgradedb));
	if( db == NULL ) {
		free(filename);
		return RET_ERROR_OOM;
	}

	if ((dbret = db_create(&db->database, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s:%s %s\n", filename,identifier,db_strerror(dbret));
		free(filename);
		free(db);
		return RET_DBERR(dbret);
	}

	/* somehow ->remove seems to be broken, or I do not understand it... 
	if( clear ) {
		dbret = db->database->remove(db->database, filename, identifier, 0);
		if( dbret != 0 ) {
			fprintf(stderr, "db_remove: %s:%s %d:%s\n", filename,identifier,dbret,db_strerror(dbret));
			(void)db->database->close(db->database,0);
			fprintf(stderr,"blub\n");
		}
	}
	*/
	
	if ((dbret = db->database->open(db->database, filename, identifier, DB_BTREE, DB_CREATE, 0664)) != 0) {
		db->database->err(db->database, dbret, "%s(%s)", filename,identifier);
		(void)db->database->close(db->database,0);
		free(filename);
		free(db);
		return RET_DBERR(dbret);
	}                     
	free(filename);
	
	*udb = db;

	/* TODO: this is a ugly workaround: */
	if( clear ) {
		DBC *cursor;DBT dummykey,dummydata;

		dbret = db->database->cursor(db->database,NULL,&cursor,0 /*DB_WRITECURSOR*/);
		if( dbret != 0 ) {
			db->database->err(db->database, dbret, ".(%s)", identifier);
		}
		while( dbret == 0 ) {
			CLEARDBT(dummykey);
			CLEARDBT(dummydata);
			dbret = cursor->c_get(cursor,&dummykey,&dummydata,DB_NEXT);
			if( dbret == DB_NOTFOUND )
				return RET_OK;
			if( dbret == 0 ) {
				dbret = cursor->c_del(cursor,0);
			}
		}
		if( dbret != 0 ) {
			db->database->err(db->database, dbret, "%d:%d:%d(%s)", DB_NOTFOUND,EINVAL,dbret,identifier);
		}
	}
	return RET_OK;
}

/* Dump the data of the database */
retvalue upgrade_dump(upgradedb db) {
	int dbret;
	DBC *cursor;DBT key,data;

	dbret = db->database->cursor(db->database,NULL,&cursor,0);
	if( dbret != 0 ) {
		db->database->err(db->database, dbret, "error opening cursor");
		return RET_DBERR(dberr);
	}
	while( dbret == 0 ) {
		CLEARDBT(key);
		CLEARDBT(data);
		dbret = cursor->c_get(cursor,&key,&data,DB_NEXT);
		if( dbret == DB_NOTFOUND )
			return RET_OK;
		if( dbret == 0 ) {
			printf("%s: ",(const char*)key.data);
			strtupel_print(stdout,(strtupel *)data.data);
			putchar('\n');
		}
	}
	db->database->err(db->database, dbret, "error dumping upgrade data:");
	return RET_DBERR(dbret);
}

static retvalue save_package_version(void *d,const char *package,const char *chunk) {
	upgradedb db = d;
	char *version;
	strtupel *tupel;
	retvalue r;
	DBT key,data;
	int dbret;

	r = chunk_getvalue(chunk,"Version",&version);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not found Version in chunk:'%s'\n",chunk);
		return RET_ERROR;
	}

	tupel = strtupel_fromvalues(3,"-",version,"(used)");
	free(version);
	if( tupel == NULL )
		return RET_ERROR_OOM;
	
	SETDBT(key,package);
	SETDBTT(data,tupel);

	if ((dbret = db->database->put(db->database, NULL, &key, &data, DB_NOOVERWRITE)) == 0) {
		fprintf(stderr,"added: '%s' \n",package);
	} else {
		db->database->err(db->database, dbret, "upgrade.db:");
		r = RET_DBERR(dberr);
	}

	free(tupel);
	return r;
}

/* Initialize a upgrade-cycle for the given distribution, getting
 * the Versions of all packages currently in it...*/
retvalue upgrade_start(upgradedb udb,packagesdb packages) {
	retvalue r;

	r = packages_foreach(packages,save_package_version,udb,0);
	return r;
}

/* Add all newer packages from the given source to the list */
retvalue upgrade_add(upgradedb udb,const char *filename) {
	return RET_ERROR;
}

/* Print information about availability and status of packages */
retvalue upgrade_dumpstatus(upgradedb udb,packagesdb packages) {
	return RET_NOTHING;
}

/* Remove all packages, that are no longer available upstream */
retvalue upgrade_deleteoldunavail(upgradedb udb,packagesdb packages) {
	return RET_ERROR;
}

/* Add all needed files to the list of files to download */
retvalue upgrade_download(upgradedb udb/*, download-db*/) {
	return RET_ERROR;
}

/* Upgrade everything assuming everything got downloaded already */
retvalue upgrade_do(upgradedb udb,packagesdb packages) {
	return RET_ERROR;
}
