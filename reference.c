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
#include "error.h"
#include "md5sum.h"
#include "dirs.h"
#include "reference.h"

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

extern int verbose;

retvalue references_done(DB *db) {
	int r;
	r = db->close(db,0);
	if( r == 0 )
		return RET_OK;
	else
		return RET_DBERR(r);
}
	
DB *references_initialize(const char *dbpath) {
	DB *dbp;
	int ret;
	char *filename;

	
	asprintf(&filename,"%s/references.db",dbpath);
	if( make_parent_dirs(filename) < 0 ) {
		free(filename);
		return NULL;
	}
	if ((ret = db_create(&dbp, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		free(filename);
		return NULL;
	}
	if ((ret = dbp->open(dbp, filename, "references", DB_BTREE, DB_CREATE, 0664)) != 0) {
		dbp->err(dbp, ret, "%s", filename);
		dbp->close(dbp,0);
		free(filename);
		return NULL;
	}
	free(filename);
	return dbp;
}

retvalue references_isused(DB *refdb,const char *what) {
	int dbret;
	DBT key,data;

	SETDBT(key,what);
	CLEARDBT(data);	
	if( (dbret = refdb->get(refdb, NULL, &key, &data, 0)) == 0){
		return RET_OK;
	} else if( dbret == DB_NOTFOUND ){
		return RET_NOTHING;
	} else {
		refdb->err(refdb, dbret, "references.db:");
		return RET_DBERR(dbret);
	}
}

retvalue references_adddependency(DB* refdb,const char *needed,const char *neededby) {
	int dbret;
	DBT key,data;

	SETDBT(key,needed);
	SETDBT(data,neededby);
	if ((dbret = refdb->put(refdb, NULL, &key, &data, 0)) == 0) {
		if( verbose > 2 )
			fprintf(stderr,"db: %s: reference by %s added.\n", needed,neededby);
		return RET_OK;
	} else {
		refdb->err(refdb, dbret, "references.db:reference:");
		return RET_DBERR(dbret);
	}
}

retvalue references_removedependency(DB* refdb,const char *neededby) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue r;

	r = RET_NOTHING;
	cursor = NULL;
	if( (dbret = refdb->cursor(refdb,NULL,&cursor,0)) != 0 ) {
		refdb->err(refdb, dbret, "references.db:reference:");
		return RET_DBERR(dbret);
	}
	CLEARDBT(key);	
	CLEARDBT(data);	
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		if( strcmp( (const char*)data.data,neededby) == 0 ) {
			if( verbose > 0 )
				fprintf(stderr,"Removing mark from file '%s' to be needed by '%s'\n",
					(const char *)key.data,neededby);
			dbret = cursor->c_del(cursor,0);
			if( dbret != 0 ) {
				refdb->err(refdb, dbret, "references.db:reference:");
				RET_UPDATE(r,RET_DBERR(dbret));
			}
		}
	}
	if( dbret != DB_NOTFOUND ) {
		refdb->err(refdb, dbret, "references.db:reference:");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refdb->err(refdb, dbret, "references.db:reference:");
		return RET_DBERR(dbret);
	}
	return r;
}

/* print out all referee-referenced-pairs. return 1 if ok, -1 on error */
retvalue references_dump(DB *refdb) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result;

	cursor = NULL;
	if( (dbret = refdb->cursor(refdb,NULL,&cursor,0)) != 0 ) {
		refdb->err(refdb, dbret, "references.db:reference:");
		return RET_DBERR(dbret);
	}
	CLEARDBT(key);	
	CLEARDBT(data);
	result = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		printf("%s %s\n",(const char*)data.data,(const char*)key.data);
		result = RET_OK;
	}
	if( dbret != DB_NOTFOUND ) {
		refdb->err(refdb, dbret, "references.db:reference:");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refdb->err(refdb, dbret, "references.db:reference:");
		return RET_DBERR(dbret);
	}
	return result;
}
