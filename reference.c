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
#include "md5sum.h"
#include "dirs.h"

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

extern int verbose;

int references_done(DB *db) {
	return db->close(db,0);
}
	
DB *references_initialize(const char *dbpath) {
	DB *dbp;
	int ret;
	char *filename;

	
	asprintf(&filename,"%s/files.db",dbpath);
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

int references_adddependency(DB* refdb,const char *needed,const char *neededby) {
	int ret;
	DBT key,data;

	SETDBT(key,needed);
	SETDBT(data,neededby);
	if ((ret = refdb->put(refdb, NULL, &key, &data, 0)) == 0) {
		if( verbose > 2 )
			fprintf(stderr,"db: %s: reference by %s added.\n", needed,neededby);
	} else {
		refdb->err(refdb, ret, "files.db:reference:");
	}
	return ret;

}

int references_removedependency(DB* refdb,const char *neededby) {
	DBC *cursor;
	DBT key,data;
	int ret, r = 0;

	cursor = NULL;
	if( (ret = refdb->cursor(refdb,NULL,&cursor,0)) != 0 ) {
		refdb->err(refdb, ret, "files.db:reference:");
		return -1;
	}
	CLEARDBT(key);	
	CLEARDBT(data);	
	while( (ret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		if( strcmp( (const char*)data.data,neededby) == 0 ) {
			if( verbose > 0 )
				fprintf(stderr,"Removing mark from file '%s' to be needed by '%s'\n",
					(const char *)key.data,neededby);
			ret = cursor->c_del(cursor,0);
			if( ret != 0 ) {
				refdb->err(refdb, ret, "files.db:reference:");
				r = -1;
			}
		}
	}
	if( ret != DB_NOTFOUND ) {
		refdb->err(refdb, ret, "files.db:reference:");
		return -1;
	}
	if( (ret = cursor->c_close(cursor)) != 0 ) {
		refdb->err(refdb, ret, "files.db:reference:");
		return -1;
	}
	return r;
}
