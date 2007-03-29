/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007 Bernhard R. Link
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
#include <db.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "md5sum.h"
#include "dirs.h"
#include "reference.h"

struct references {
	DB *db;
};

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}

extern int verbose;

retvalue references_done(references refs) {
	int r;
	if( refs == NULL )
		return RET_NOTHING;
	r = refs->db->close(refs->db,0);
	free(refs);
	if( r == 0 )
		return RET_OK;
	else
		return RET_DBERR(r);
}

retvalue references_initialize(references *refs,const char *dbpath) {
	int ret;
	char *filename;
	retvalue r;
	references ref;

	/* what stupidities are done for abstraction... */
	ref = malloc(sizeof(*ref));
	if( ref == NULL )
		return RET_ERROR_OOM;

	filename = calc_dirconcat(dbpath,"references.db");
	if( filename == NULL ) {
		free(ref);
		return RET_ERROR_OOM;
	}
	r = dirs_make_parent(filename);
	if( RET_WAS_ERROR(r) ) {
		free(filename);
		free(ref);
		return RET_ERROR_OOM;
	}
	if( (ret = db_create(&ref->db, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		free(filename);
		free(ref);
		return RET_DBERR(ret);
	}
	/* allow a file referenced by multiple dists: */
	if( (ret = ref->db->set_flags(ref->db,DB_DUPSORT)) != 0 ) {
		ref->db->err(ref->db, ret, "db_set_flags:%s", filename);
		(void)ref->db->close(ref->db,0);
		free(filename);
		free(ref);
		return RET_DBERR(ret);
	}
	ret = DB_OPEN(ref->db, filename, "references", DB_BTREE, DB_CREATE);
	if( ret != 0) {
		ref->db->err(ref->db, ret, "db_open:%s", filename);
		(void)ref->db->close(ref->db,0);
		free(filename);
		free(ref);
		return RET_DBERR(ret);
	}
	free(filename);
	*refs = ref;
	return RET_OK;
}

retvalue references_isused(references refs,const char *what) {
	int dbret;
	DBT key,data;

	SETDBT(key,what);
	CLEARDBT(data);
	if( (dbret = refs->db->get(refs->db, NULL, &key, &data, 0)) == 0){
		return RET_OK;
	} else if( dbret == DB_NOTFOUND ){
		return RET_NOTHING;
	} else {
		refs->db->err(refs->db, dbret, "references.db:");
		return RET_DBERR(dbret);
	}
}

static retvalue references_checksingle(references refs,const char *what,const char *by) {
	int dbret;
	retvalue r;
	DBT key,data;
	DBC *cursor;

	cursor = NULL;
	if( (dbret = refs->db->cursor(refs->db,NULL,&cursor,0)) != 0 ) {
		refs->db->err(refs->db, dbret, "references_check dberror:");
		return RET_DBERR(dbret);
	}
	SETDBT(key,what);
	SETDBT(data,by);
	if( (dbret=cursor->c_get(cursor,&key,&data,DB_GET_BOTH)) == 0 ) {
		r = RET_OK;
	} else
	if( dbret != DB_NOTFOUND ) {
		refs->db->err(refs->db, dbret, "references_check dberror(get):");
		return RET_DBERR(dbret);
	} else {
		fprintf(stderr,"Missing reference to '%s' by '%s'\n",what,by);
		r = RET_ERROR;
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refs->db->err(refs->db, dbret, "references_check dberror(cl):");
		return RET_DBERR(dbret);
	}
	return r;
}
retvalue references_check(references refs,const char *referee,const struct strlist *filekeys) {
	int i;
	retvalue ret,r;

	ret = RET_NOTHING;
	for( i = 0 ; i < filekeys->count ; i++ ) {
		r = references_checksingle(refs,filekeys->values[i],referee);
		RET_UPDATE(ret,r);

	}
	return ret;
}

/* add an reference to a file for an identifier. multiple calls */
retvalue references_increment(references refs,const char *needed,const char *neededby) {
	int dbret;
	DBT key,data;

	SETDBT(key,needed);
	SETDBT(data,neededby);
	if ((dbret = refs->db->put(refs->db, NULL, &key, &data, 0)) == 0) {
		if( verbose > 8 )
			printf("Adding reference to '%s' by '%s'\n", needed,neededby);
		return RET_OK;
	} else {
		refs->db->err(refs->db, dbret, "references_increment dberror:");
		return RET_DBERR(dbret);
	}
}

/* remove reference for a file from a given reference */
retvalue references_decrement(references refs,const char *needed,const char *neededby) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue r;

	r = RET_OK;
	cursor = NULL;
	if( (dbret = refs->db->cursor(refs->db,NULL,&cursor,0)) != 0 ) {
		refs->db->err(refs->db, dbret, "references_decrement dberror:");
		return RET_DBERR(dbret);
	}
	SETDBT(key,needed);
	SETDBT(data,neededby);
	if( (dbret=cursor->c_get(cursor,&key,&data,DB_GET_BOTH)) == 0 ) {
			if( verbose > 8 )
				fprintf(stderr,"Removing reference to '%s' by '%s'\n",
					(const char *)key.data,neededby);
			dbret = cursor->c_del(cursor,0);
			if( dbret != 0 ) {
				refs->db->err(refs->db, dbret, "references_decrement dberror(del):");
				RET_UPDATE(r,RET_DBERR(dbret));
			}
	} else
	if( dbret != DB_NOTFOUND ) {
		refs->db->err(refs->db, dbret, "references_decrement dberror(get):");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refs->db->err(refs->db, dbret, "references_decrement dberror(cl):");
		return RET_DBERR(dbret);
	}
	return r;
}

/* Add an reference by <identifer> for the given <files>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_insert(references refs,const char *identifier,
		const struct strlist *files,const struct strlist *exclude) {
	retvalue result,r;
	int i;

	result = RET_NOTHING;

	for( i = 0 ; i < files->count ; i++ ) {
		const char *filename = files->values[i];

		if( exclude == NULL || !strlist_in(exclude,filename) ) {
			r = references_increment(refs,filename,identifier);
			RET_UPDATE(result,r);
		}
	}
	return result;
}

/* add possible already existing references */
retvalue references_add(references refs,const char *identifier,const struct strlist *files) {
	int dbret;
	int i;
	DBT key,data;

	for( i = 0 ; i < files->count ; i++ ) {
		const char *filekey = files->values[i];
		SETDBT(key,filekey);
		SETDBT(data,identifier);
		if ((dbret = refs->db->put(refs->db, NULL, &key, &data, DB_NODUPDATA)) == 0) {
			if( verbose > 8 )
				fprintf(stderr,"Added reference to '%s' by '%s'\n", filekey, identifier);
		} else if( dbret != DB_KEYEXIST) {
			refs->db->err(refs->db, dbret, "references_add dberror:");
			return RET_DBERR(dbret);
		}
	}
	return RET_OK;
}

/* Remove reference by <identifer> for the given <oldfiles>,
 * excluding <exclude>, if it is nonNULL. */
retvalue references_delete(references refs,const char *identifier,
		struct strlist *files,const struct strlist *exclude,
		struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	int i;

	assert( files != NULL );

	result = RET_NOTHING;

	for( i = 0 ; i < files->count ; i++ ) {
		char *filekey = files->values[i];
		files->values[i] = NULL;

		if( exclude == NULL || !strlist_in(exclude,filekey) ) {
			r = references_decrement(refs,filekey,identifier);
			RET_UPDATE(result,r);
			if( RET_IS_OK(r) && dereferencedfilekeys != NULL ) {
				r = strlist_adduniq(dereferencedfilekeys, filekey);
				RET_UPDATE(result,r);
			} else
				free(filekey);
		} else
			free(filekey);
	}
	strlist_done(files);
	return result;

}

/* remove all references from a given identifier */
retvalue references_remove(references refs,const char *neededby,
		struct strlist *dereferenced) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue r;
	size_t l;

	r = RET_NOTHING;
	cursor = NULL;
	if( (dbret = refs->db->cursor(refs->db,NULL,&cursor,0)) != 0 ) {
		refs->db->err(refs->db, dbret, "references_remove dberror(cursor):");
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
			if( verbose > 8 )
				fprintf(stderr,"Removing reference to '%s' by '%s'\n",
					found_to,neededby);
			dbret = cursor->c_del(cursor,0);
			if( dbret == 0 ) {
				if( dereferenced != NULL ) {
					char *f = strndup(key.data,key.size-1);
					retvalue r2;
					if( f == NULL ) {
						r = RET_ERROR_OOM;
						continue;
					}
					r2 = strlist_add(dereferenced, f);
					if( RET_WAS_ERROR(r2) ) {
						r = r2;
					}
				}
			} else {
				refs->db->err(refs->db, dbret, "references_remove dberror(del):");
				RET_UPDATE(r,RET_DBERR(dbret));
			}
		}
	}
	if( dbret != DB_NOTFOUND ) {
		refs->db->err(refs->db, dbret, "references_remove dberror(get):");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refs->db->err(refs->db, dbret, "references_remove dberror(cl):");
		return RET_DBERR(dbret);
	}
	return r;
}

/* print out all referee-referenced-pairs. */
retvalue references_dump(references refs) {
	DBC *cursor;
	DBT key,data;
	int dbret;
	retvalue result;

	cursor = NULL;
	if( (dbret = refs->db->cursor(refs->db,NULL,&cursor,0)) != 0 ) {
		refs->db->err(refs->db, dbret, "references_dump dberror(cursor):");
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
		refs->db->err(refs->db, dbret, "references_dump dberror(get):");
		return RET_DBERR(dbret);
	}
	if( (dbret = cursor->c_close(cursor)) != 0 ) {
		refs->db->err(refs->db, dbret, "references_dump dberror:");
		return RET_DBERR(dbret);
	}
	return result;
}
