/*  This file is part of "reprepro"
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
#include "strlist.h"
#include "names.h"
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
	retvalue r;
	
	filename = calc_dirconcat(dbpath,"references.db");
	if( !filename )
		return NULL;
	r = dirs_make_parent(filename);
	if( RET_WAS_ERROR(r) ) {
		free(filename);
		return NULL;
	}
	if( (ret = db_create(&dbp, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		free(filename);
		return NULL;
	}
	/* allow a file referenced by multiple dists: */
	if( (ret = dbp->set_flags(dbp,DB_DUPSORT)) != 0 ) {
		dbp->err(dbp, ret, "db_set_flags:%s", filename);
		(void)dbp->close(dbp,0);
		free(filename);
		return NULL;
	}
	if( (ret = dbp->open(dbp, filename, "references", DB_BTREE, DB_CREATE, 0664)) != 0) {
		dbp->err(dbp, ret, "db_open:%s", filename);
		(void)dbp->close(dbp,0);
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

static retvalue references_checksingle(DB *refdb,const char *what,const char *by) {
	int dbret;
	retvalue r;
	DBT key,data;
	DBC *cursor;

	cursor = NULL;
	if( (dbret = refdb->cursor(refdb,NULL,&cursor,0)) != 0 ) {
		refdb->err(refdb, dbret, "references_check dberror:");
		return RET_DBERR(dbret);
	}
	SETDBT(key,what);	
	SETDBT(data,by);	
	if( (dbret=cursor->c_get(cursor,&key,&data,DB_GET_BOTH)) == 0 ) {
		r = RET_OK;
	} else
	if( dbret != DB_NOTFOUND ) {
		refdb->err(refdb, dbret, "references_check dberror(get):");
		return RET_DBERR(dbret);
	} else {
		fprintf(stderr,"Missing reference to '%s' by '%s'\n",what,by);
		r = RET_ERROR;
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refdb->err(refdb, dbret, "references_check dberror(cl):");
		return RET_DBERR(dbret);
	}
	return r;
}
retvalue references_check(DB *refdb,const char *referee,const struct strlist *filekeys) {
	int i;
	retvalue ret,r;

	ret = RET_NOTHING;
	for( i = 0 ; i < filekeys->count ; i++ ) {
		r = references_checksingle(refdb,filekeys->values[i],referee);
		RET_UPDATE(ret,r);
		
	}
	return ret;
}

/* add an reference to a file for an identifier. multiple calls
 * will add multiple references to allow source packages to share
 * files over versions. (as first the new is added, then the old removed) */
retvalue references_increment(DB* refdb,const char *needed,const char *neededby) {
	int dbret;
	DBT key,data;

	SETDBT(key,needed);
	SETDBT(data,neededby);
	if ((dbret = refdb->put(refdb, NULL, &key, &data, 0)) == 0) {
		if( verbose > 8 )
			fprintf(stderr,"Adding reference to '%s' by '%s'\n", needed,neededby);
		return RET_OK;
	} else {
		refdb->err(refdb, dbret, "references_increment dberror:");
		return RET_DBERR(dbret);
	}
}

/* remove *one* reference for a file from a given reference */
retvalue references_decrement(DB* refdb,const char *needed,const char *neededby) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue r;

	r = RET_NOTHING;
	cursor = NULL;
	if( (dbret = refdb->cursor(refdb,NULL,&cursor,0)) != 0 ) {
		refdb->err(refdb, dbret, "references_decrement dberror:");
		return RET_DBERR(dbret);
	}
	SETDBT(key,needed);	
	SETDBT(data,neededby);	
	if( (dbret=cursor->c_get(cursor,&key,&data,DB_GET_BOTH)) == 0 ) {
			if( verbose > 5 )
				fprintf(stderr,"Removing reference to '%s' by '%s'\n",
					(const char *)key.data,neededby);
			dbret = cursor->c_del(cursor,0);
			if( dbret != 0 ) {
				refdb->err(refdb, dbret, "references_decrement dberror(del):");
				RET_UPDATE(r,RET_DBERR(dbret));
			}
	} else 
	if( dbret != DB_NOTFOUND ) {
		refdb->err(refdb, dbret, "references_decrement dberror(get):");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refdb->err(refdb, dbret, "references_decrement dberror(cl):");
		return RET_DBERR(dbret);
	}
	return r;
}

/* Add an reference by <identifer> for the given <files>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_insert(DB *refdb,const char *identifier,
		const struct strlist *files,const struct strlist *exclude) {
	retvalue result,r;
	int i;

	result = RET_NOTHING;

	for( i = 0 ; i < files->count ; i++ ) {
		const char *filename = files->values[i];

		if( exclude == NULL || !strlist_in(exclude,filename) ) {
			r = references_increment(refdb,filename,identifier);
			RET_UPDATE(result,r);
		}
	}
	return result;
}

/* Remove reference by <identifer> for the given <oldfiles>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_delete(DB *refdb,const char *identifier,
		const struct strlist *files,const struct strlist *exclude) {
	retvalue result,r;
	int i;

	result = RET_NOTHING;

	for( i = 0 ; i < files->count ; i++ ) {
		const char *filename = files->values[i];

		if( exclude == NULL || !strlist_in(exclude,filename) ) {
			r = references_decrement(refdb,filename,identifier);
			RET_UPDATE(result,r);
		}
	}
	return result;
	
}

/* remove all references from a given identifier */
retvalue references_remove(DB* refdb,const char *neededby) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue r;
	size_t l;

	r = RET_NOTHING;
	cursor = NULL;
	if( (dbret = refdb->cursor(refdb,NULL,&cursor,0)) != 0 ) {
		refdb->err(refdb, dbret, "references_remove dberror(cursor):");
		return RET_DBERR(dbret);
	}
	l = strlen(neededby);
	CLEARDBT(key);	
	CLEARDBT(data);	
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		const char *found_to = key.data;
		const char *found_by = data.data;
		if( strncmp( found_by,neededby,l) == 0 && 
		    (found_by[l] == '\0' || found_by[l] == ' ')) {
			if( verbose > 5 )
				fprintf(stderr,"Removing reference to '%s' by '%s'\n",
					found_to,neededby);
			dbret = cursor->c_del(cursor,0);
			if( dbret != 0 ) {
				refdb->err(refdb, dbret, "references_remove dberror(del):");
				RET_UPDATE(r,RET_DBERR(dbret));
			}
		}
	}
	if( dbret != DB_NOTFOUND ) {
		refdb->err(refdb, dbret, "references_remove dberror(get):");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refdb->err(refdb, dbret, "references_remove dberror(cl):");
		return RET_DBERR(dbret);
	}
	return r;
}

/* print out all referee-referenced-pairs. */
retvalue references_dump(DB *refdb) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result;

	cursor = NULL;
	if( (dbret = refdb->cursor(refdb,NULL,&cursor,0)) != 0 ) {
		refdb->err(refdb, dbret, "references_dump dberror(cursor):");
		return RET_DBERR(dbret);
	}
	CLEARDBT(key);	
	CLEARDBT(data);
	result = RET_NOTHING;
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		const char *found_to = (const char*)key.data;
		const char *found_by = (const char*)data.data;
		if( fputs(found_by,stdout) == EOF || 
		    putchar(' ') == EOF ||
		    puts(found_to) == EOF ) {
			result = RET_ERROR;
			break;
		}
		result = RET_OK;
	}
	if( dbret != DB_NOTFOUND ) {
		refdb->err(refdb, dbret, "references_dump dberror(get):");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refdb->err(refdb, dbret, "references_dump dberror:");
		return RET_DBERR(dbret);
	}
	return result;
}
