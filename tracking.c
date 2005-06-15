/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
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
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <db.h>

#include "error.h"
#include "names.h"
#include "dirs.h"
#include "names.h"

#include "tracking.h"

extern int verbose;

#ifndef NOPARANOIA
#define PARANOIA
#endif

struct s_tracking {
	char *codename;
	DB *db;
};

#define CLEARDBT(dbt) {memset(&dbt,0,sizeof(dbt));}
#define SETDBT(dbt,datastr) {const char *my = datastr;memset(&dbt,0,sizeof(dbt));dbt.data=(void *)my;dbt.size=strlen(my)+1;}


retvalue tracking_done(trackingdb db) {
	int dbret;

	if( db == NULL )
		return RET_OK;

	dbret = db->db->close(db->db,0);
	free(db->codename);
	free(db);
	if( dbret < 0 )
		return RET_DBERR(dbret);
	else
		return RET_OK;
}

static int mydatacompare(UNUSED(DB *db), const DBT *a, const DBT *b) {
	if( a->size < b->size )
		return strncmp(a->data,b->data,a->size);
	else
		return strncmp(a->data,b->data,b->size);
}


retvalue tracking_initialize(/*@out@*/trackingdb *db,const char *dbpath,const struct distribution *distribution) {
	char *filename;
	struct s_tracking *t;
	retvalue r;
	int dbret;

	filename = calc_dirconcat(dbpath,"tracking.db");
	if( filename == NULL )
		return RET_ERROR_OOM;
	r = dirs_make_parent(filename);
	if( RET_WAS_ERROR(r) ) {
		free(filename);
		return r;
	}
	t = calloc(1,sizeof(struct s_tracking));
	if( t == NULL ) {
		free(filename);
		return RET_ERROR_OOM;
	}
	t->codename = strdup(distribution->codename);
	if( t->codename == NULL ) {
		free(filename);
		free(t);
		return RET_ERROR_OOM;
	}
	if ((dbret = db_create(&t->db, NULL, 0)) != 0) {
		fprintf(stderr, "db_create: %s:%s %s\n", 
				filename,t->codename,db_strerror(dbret));
		free(filename);
		free(t->codename);
		free(t);
		return RET_DBERR(dbret);
	}
	/* allow multiple versions of a package */
	if( (dbret = t->db->set_flags(t->db,DB_DUPSORT)) != 0 ) {
		t->db->err(t->db,dbret,"db_set_flags:%s:%s",filename,t->codename);
		(void)t->db->close(t->db,0);
		free(filename);
		free(t->codename);
		free(t);
		return RET_DBERR(dbret);
	}
	/* sort by sorting the version string */
	if( (dbret = t->db->set_dup_compare(t->db,mydatacompare)) != 0 ) {
		t->db->err(t->db,dbret,"db_set_dup_compare:%s:%s",filename,t->codename);
		(void)t->db->close(t->db,0);
		free(filename);
		free(t->codename);
		free(t);
		return RET_DBERR(dbret);
	}

	if ((dbret = t->db->open(t->db,filename,t->codename,
					DB_BTREE,DB_CREATE,0664)) != 0 ) {
		t->db->err(t->db, dbret, "db_open:%s:%s", filename, t->codename);
		(void)t->db->close(t->db,0);
		free(filename);
		free(t->codename);
		free(t);
		return RET_DBERR(ret);
	}
	free(filename);

	*db = t;
	return RET_OK;
}

static inline char filetypechar(enum filetype filetype) {
	switch( filetype ) {
		case ft_CHANGES:
		case ft_ALL_BINARY:
		case ft_ARCH_BINARY:
		case ft_SOURCE:
		case ft_XTRA_DATA:
			return filetype;
	}
	assert(FALSE);
	return 'x';
}

retvalue trackedpackage_addfilekey(trackingdb tracks,struct trackedpackage *pkg,enum filetype filetype,const char *filekey,references refs) {
	char *annotatedfilekey,*id;
	size_t len;
	char ft = filetypechar(filetype);
	int i, *newrefcounts;
	retvalue r;

	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		if( strcmp(pkg->filekeys.values[i]+1,filekey) == 0 ) {
			if( pkg->filekeys.values[i][0] != ft ) {
				fprintf(stderr,"Filekey '%s' already registered for '%s_%s' as type '%c' is tried to be reregistered as type '%c'!\n",filekey,pkg->sourcename,pkg->sourceversion,pkg->filekeys.values[i][0],ft);
				return RET_ERROR;
			}
			pkg->refcounts[i]++;
			return RET_OK;
		}
	}

	len = strlen(filekey);

	annotatedfilekey = malloc(len+2);
	if( annotatedfilekey == NULL )
		return RET_ERROR_OOM;

	annotatedfilekey[0] = filetypechar(filetype);
	memcpy(annotatedfilekey+1,filekey,len);
	annotatedfilekey[len+1] = '\0';

	newrefcounts = realloc(pkg->refcounts,sizeof(int)*(pkg->filekeys.count+1));
	if( newrefcounts == NULL ) {
		free(annotatedfilekey);
		return RET_ERROR_OOM;
	}
	newrefcounts[pkg->filekeys.count]=1;
	pkg->refcounts = newrefcounts;

	r = strlist_add(&pkg->filekeys,annotatedfilekey);
	if( RET_WAS_ERROR(r) )
		return r;

	id = calc_trackreferee(tracks->codename,pkg->sourcename,pkg->sourceversion);
	if( id == NULL )
		return RET_ERROR_OOM;
	r = references_increment(refs,filekey,id);
	free(id);
	return r;
}

retvalue trackedpackage_addfilekeys(trackingdb tracks,struct trackedpackage *pkg,enum filetype filetype,const struct strlist *filekeys,references refs) {
	int i;
	retvalue result,r;
	assert( filekeys != NULL );

	result = RET_OK;
	for( i = 0 ; i < filekeys->count ; i++ ) {
		r = trackedpackage_addfilekey(tracks,pkg,filetype,filekeys->values[i],refs);
		RET_UPDATE(result,r);
	}
	return result;
}

static inline retvalue trackedpackage_removefilekey(trackingdb tracks,struct trackedpackage *pkg,const char *filekey) {
	int i;

	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		if( strcmp(pkg->filekeys.values[i]+1,filekey) == 0 ) {
			if( pkg->refcounts[i] > 0 )
				pkg->refcounts[i]--;
			else
				fprintf(stderr,"Warning: tracking database of %s has inconsistent refcounts of %s_%s.\n",tracks->codename,pkg->sourcename,pkg->sourceversion);
				
			return RET_OK;
		}
	}
	fprintf(stderr,"Warning: tracking database of %s missed files for %s_%s.\n",tracks->codename,pkg->sourcename,pkg->sourceversion);
	return RET_OK;

}

retvalue trackedpackage_removefilekeys(trackingdb tracks,struct trackedpackage *pkg,const struct strlist *filekeys) {
	int i;
	retvalue result,r;
	assert( filekeys != NULL );

	result = RET_OK;
	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *filekey = filekeys->values[i];
		r = trackedpackage_removefilekey(tracks,pkg,filekey);
		RET_UPDATE(result,r);
	}
	return result;
}

void trackedpackage_free(struct trackedpackage *pkg) {
	if( pkg != NULL ) {
		free(pkg->sourcename);
		free(pkg->sourceversion);
		strlist_done(&pkg->filekeys);
		free(pkg->refcounts);
		free(pkg);
	}
}

static inline int parsenumber(const char **d,size_t *s) {
	int count;
	
	count = 0;
	do {
		if( **d < '0' || **d > '7' )
			return -1;
		count = (count*8) + (**d-'0');
		(*d)++;
		(*s)--;
		if( *s == 0 )
			return -1;
	} while( **d != '\0' );
	(*d)++;
	(*s)--;
	return count;
}

retvalue tracking_new(trackingdb t,const char *sourcename,const char *version,/*@out@*/struct trackedpackage **pkg) {
	struct trackedpackage *p;
	assert( pkg != NULL && sourcename != NULL && version != NULL );

	printf("[tracing_new %s %s %s]\n",t->codename,sourcename,version);
	p = calloc(1,sizeof(struct trackedpackage));
	if( p == NULL )
		return RET_ERROR_OOM;
	p->sourcename = strdup(sourcename);
	p->sourceversion = strdup(version);
	if( p->sourcename == NULL || p->sourceversion == NULL ) {
		trackedpackage_free(p);
		return RET_ERROR_OOM;
	}
	*pkg = p;
	return RET_OK;
}

static inline int search(DBC *cursor,DBT *key,DBT *data,const char *version, size_t versionlen) {
	const char *d;
	int dbret;

	CLEARDBT(*data);
	dbret=cursor->c_get(cursor,key,data,DB_SET);
	while( TRUE ) {
		if( dbret != 0 ) {
			return dbret;
		}
		d = data->data;
		/* Check if this is the version we are looking for.  *
		 * (this compares versions literally, not by value!) */
		if( data->size > versionlen+1 && 
				strncmp(d,version,versionlen) == 0 && 
				d[versionlen] == '\0' )
			break;

		CLEARDBT(*data);
		dbret = cursor->c_get(cursor,key,data,DB_NEXT_DUP);
	}
	return 0;

}

static inline retvalue parsedata(const char *name,const char *version,size_t versionlen,DBT data,/*@out@*/struct trackedpackage **pkg) {
	struct trackedpackage *p;
	const char *d;
	int i;

	d = data.data;
	p = calloc(1,sizeof(struct trackedpackage));
	if( p == NULL )
		return RET_ERROR_OOM;
	p->sourcename = strdup(name);
	p->sourceversion = strdup(version);
	d += versionlen+1;
	data.size -= versionlen+1;
	if( p->sourcename == NULL || p->sourceversion == NULL /*||
				     p->sourcedir == NULL */ ) {
		trackedpackage_free(p);
		return RET_ERROR_OOM;
	}
	while( *d != '\0' && data.size > 0 ) {
		char *filekey;
		retvalue r;

		filekey = strndup(d,data.size-1);
		if( filekey == NULL ) {
			trackedpackage_free(p);
			return RET_ERROR_OOM;
		}
		r = strlist_add(&p->filekeys,filekey);
		if( RET_WAS_ERROR(r) ) {
			trackedpackage_free(p);
			return r;
		}
		d += strlen(filekey)+1;
		data.size -= strlen(filekey)+1;
	}
	d++,data.size--;
	p->refcounts = calloc(p->filekeys.count,sizeof(int));
	if( p->refcounts == NULL ) {
		trackedpackage_free(p);
		return RET_ERROR_OOM;
	}
	for( i = 0 ; i < p->filekeys.count ; i++ ) {
		if( (p->refcounts[i] = parsenumber(&d,&data.size)) < 0 ) {
			fprintf(stderr,"Internal Error: Corrupt tracking data for %s %s\n",name,version);
			trackedpackage_free(p);
			return RET_ERROR;
		}
	}
	if( data.size > 1 ) {
		fprintf(stderr,"Internal Error: Trailing garbage in tracking data for %s %s\n (%ld bytes)",name,version,(long)data.size);
		trackedpackage_free(p);
		return RET_ERROR;
	}
	*pkg = p;
	return RET_OK;
}

	
retvalue tracking_get(trackingdb t,const char *sourcename,const char *version,/*@out@*/struct trackedpackage **pkg) {
	int dbret;
	DBT key,data;
	DBC *cursor;
	size_t versionlen;
	retvalue r;


	assert( pkg != NULL && sourcename != NULL && version != NULL );
	printf("[tracing_get %s %s %s]\n",t->codename,sourcename,version);

	cursor = NULL;
	if( (dbret = t->db->cursor(t->db,NULL,&cursor,0)) != 0 ) {
		t->db->err(t->db, dbret, "tracking_get dberror:");
		return RET_DBERR(dbret);
	}
	versionlen = strlen(version);
	SETDBT(key,sourcename);
	dbret = search(cursor,&key,&data,version,versionlen);
	if( dbret != 0 ) {
		(void)cursor->c_close(cursor);
		if( dbret == DB_KEYEMPTY || dbret == DB_NOTFOUND )
			return RET_NOTHING;
		else {
			t->db->err(t->db, dbret, "tracking_get dberror(get):");
			return RET_DBERR(dbret);
		}
	}

	printf("[tracing_get found %s %s %s]\n",t->codename,sourcename,version);
	/* we have found it, now parse it */
	r = parsedata(sourcename,version,versionlen,data,pkg);
	(void)cursor->c_close(cursor);
	return r;
}

static retvalue gendata(DBT *data,struct trackedpackage *pkg) {
	size_t 	versionsize = strlen(pkg->sourceversion)+1,
		/*dirsize = strlen(pkg->sourcedir)+1,*/
		filekeysize;
	int i;
	char *d;
	CLEARDBT(*data);

	filekeysize = 2;
	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		size_t l;
		l = strlen(pkg->filekeys.values[i]);
		if( l > 1 )
			filekeysize += l+8;
	}
	data->size = versionsize+/*dirsize+*/filekeysize;
	data->data = d = malloc(data->size);
	if( d == NULL )
		return RET_ERROR_OOM;
	memcpy(d,pkg->sourceversion,versionsize);d+=versionsize;
	/*memcpy(d,pkg->sourcedir,dirsize);d+=dirsize;*/
	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		size_t l;
		l = strlen(pkg->filekeys.values[i]);
//		printf("[save %s]\n",pkg->filekeys.values[i]);
		if( l > 1 ) {
			memcpy(d,pkg->filekeys.values[i],l+1);
			d+=l+1;
		}
	}
	*d ='\0';d++;
	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		int j;
#define MAXREFCOUNTOCTETS 7
		char countstring[MAXREFCOUNTOCTETS];
		size_t count = pkg->refcounts[i];

		countstring[MAXREFCOUNTOCTETS-1] = '\0';
		for( j = MAXREFCOUNTOCTETS-2 ; j >= 0 ; j-- ) {
			countstring[j] = '0' + (count & 7);
			count >>= 3;
			if( count == 0 )
				break;
		}
#undef MAXREFCOUNTOCTETS
		assert( count == 0 );
//		printf("[made %d to %s]\n",pkg->refcounts[i],countstring+j);

		memcpy(d,countstring+j,7-j);
		d+=7-j;
		data->size -= j;
	}
	*d ='\0';
	assert( (size_t)(((void*)d)-(data->data)) == data->size-1 );
	return RET_OK;
}

retvalue tracking_put(trackingdb t,struct trackedpackage *pkg) {
	DBT key,data;
	int dbret;
	retvalue r;

	printf("[tracing_put %s %s %s]\n",t->codename,pkg->sourcename,pkg->sourceversion);
	SETDBT(key,pkg->sourcename);
	r = gendata(&data,pkg);
	if( RET_WAS_ERROR(r) )
		return r;

	/* this should be really only called if we are sure there is
	 * not yet one of the same version in there. This will not
	 * give an error now, but strange behaviour later */

	if ((dbret = t->db->put(t->db, NULL, &key, &data, 0)) == 0) {
		free(data.data);
		if( verbose > 18 )
			fprintf(stderr,"Adding tracked package '%s'_'%s' to '%s'\n",pkg->sourcename,pkg->sourceversion,t->codename);
		return RET_OK;
	} else {
		free(data.data);
		t->db->err(t->db, dbret, "tracking_put dberror:%s:",t->codename);
		return RET_DBERR(dbret);
	}
}

retvalue tracking_replace(trackingdb t,struct trackedpackage *pkg) {
	int dbret;
	DBT key,data;
	DBC *cursor;
	retvalue r;


	assert( pkg != NULL );
	printf("[tracing_replace %s %s %s]\n",t->codename,pkg->sourcename,pkg->sourceversion);

	cursor = NULL;
	if( (dbret = t->db->cursor(t->db,NULL,&cursor,0)) != 0 ) {
		t->db->err(t->db, dbret, "tracking_replace dberror:");
		return RET_DBERR(dbret);
	}
	SETDBT(key,pkg->sourcename);
	dbret = search(cursor,&key,&data,pkg->sourceversion,strlen(pkg->sourceversion));
	if( dbret != 0 ) {
		(void)cursor->c_close(cursor);
		if( dbret == DB_KEYEMPTY || dbret == DB_NOTFOUND ) {
			fprintf(stderr,"tracking_replace called but could not find %s_%s in %s!\n",pkg->sourcename,pkg->sourceversion,t->codename);
			return tracking_put(t,pkg);
		} else {
			t->db->err(t->db, dbret, "tracking_replace dberror(get):");
			return RET_DBERR(dbret);
		}
	}
	/* found it, now replace it */

	r = gendata(&data,pkg);
	if( RET_WAS_ERROR(r) ) {
		(void)cursor->c_close(cursor);
		return r;
	}

	if( (dbret = cursor->c_put(cursor,&key,&data,DB_CURRENT)) != 0 ) {
		(void)cursor->c_close(cursor);
		t->db->err(t->db, dbret, "tracking_replace dberror(get):");
		return RET_DBERR(dbret);
	}
	return RET_OK;
}

retvalue tracking_clearall(trackingdb t) {
	DBC *cursor;
	DBT key,data;
	retvalue r;
	int dbret;

	cursor = NULL;
	if( (dbret = t->db->cursor(t->db,NULL,&cursor,0)) != 0 ) {
		t->db->err(t->db, dbret, "tracking_clearall dberror(cursor):");
		return RET_DBERR(dbret);
	}
	r = RET_OK;
	CLEARDBT(key);  
	CLEARDBT(data); 
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		dbret = cursor->c_del(cursor,0);
		if( dbret != 0 ) {
			t->db->err(t->db, dbret, "tracking_clearall dberror(del):");
			RET_UPDATE(r,RET_DBERR(dbret));
		}
	}
	if( dbret != DB_NOTFOUND ) {
		(void)cursor->c_close(cursor);
		t->db->err(t->db, dbret, "tracking_clearall dberror(get):");
		return RET_DBERR(dbret);
	}
	if( (dbret=cursor->c_close(cursor)) != 0 ) {
		t->db->err(t->db, dbret, "tracking_clearall dberror(close):");
		return RET_DBERR(dbret);
	}
	return r;
}

retvalue tracking_remove(trackingdb t,const char *sourcename,const char *version,references refs,/*@null@*/struct strlist *dereferencedfilekeys) {
	DBC *cursor;
	DBT key,data;
	retvalue result,r;
	int dbret;

	cursor = NULL;
	if( (dbret = t->db->cursor(t->db,NULL,&cursor,0)) != 0 ) {
		t->db->err(t->db, dbret, "tracking_remove dberror(cursor):");
		return RET_DBERR(dbret);
	}
	result = RET_OK;
	SETDBT(key,sourcename);
	dbret = search(cursor,&key,&data,version,strlen(version));
	if( dbret == 0 ) {
		char *id;
		struct trackedpackage *pkg;

		id = calc_trackreferee(t->codename,sourcename,version);
		if( id == NULL )
			return RET_ERROR_OOM;

		r = parsedata(sourcename,version,strlen(version),data,&pkg);
		if( RET_IS_OK(r) ) {
			int i;
			for( i = 0; i < pkg->filekeys.count ; i++ ) {
				const char *filekey = pkg->filekeys.values[i]+1;

				r = references_decrement(refs,filekey,id);
				RET_UPDATE(result,r);
				if( dereferencedfilekeys != NULL )
					r = strlist_adduniq(dereferencedfilekeys,strdup(filekey));
				RET_UPDATE(result,r);
			}
			trackedpackage_free(pkg);

		} else {
			RET_UPDATE(result,r);
			fprintf(stderr,"Could not parse data, removing all references blindly...\n");
			r = references_remove(refs,id);
			RET_UPDATE(result,r);
		}
		free(id);
		dbret = cursor->c_del(cursor,0);
		if( dbret != 0 ) {
			t->db->err(t->db, dbret, "tracking_remove dberror(del):");
			RET_UPDATE(result,RET_DBERR(dbret));
		} else {
			fprintf(stderr,"Removed %s_%s from %s.\n",sourcename,version,t->codename);
		}
	} else if( dbret != DB_NOTFOUND ) {
		(void)cursor->c_close(cursor);
		t->db->err(t->db, dbret, "tracking_remove dberror(get):");
		return RET_DBERR(dbret);
	} else
		result = RET_NOTHING;
	if( (dbret=cursor->c_close(cursor)) != 0 ) {
		t->db->err(t->db, dbret, "tracking_remove dberror(close):");
		return RET_DBERR(dbret);
	}
	return result;
}

static void print(const char *codename,const struct trackedpackage *pkg){
	int i;

	printf("Distribution: %s\n",codename);
	printf("Source: %s\n",pkg->sourcename);
	printf("Version: %s\n",pkg->sourceversion);
	printf("Files:\n");
	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		const char *filekey = pkg->filekeys.values[i];

		printf(" %s %c %d\n",filekey+1,filekey[0],pkg->refcounts[i]);
	}
	fputs("\n",stdout);
}
	
retvalue tracking_printall(trackingdb t) {
	DBC *cursor;
	DBT key,data;
	retvalue r;
	int dbret;

	cursor = NULL;
	if( (dbret = t->db->cursor(t->db,NULL,&cursor,0)) != 0 ) {
		t->db->err(t->db, dbret, "tracking_printall dberror(cursor):");
		return RET_DBERR(dbret);
	}
	r = RET_OK;
	CLEARDBT(key);  
	CLEARDBT(data); 
	while( (dbret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		struct trackedpackage *pkg;

		r = parsedata(key.data,data.data,strlen(data.data),data,&pkg);
		if( RET_IS_OK(r) ) {
			print(t->codename,pkg);
			trackedpackage_free(pkg);
		}
	}
	if( dbret != DB_NOTFOUND ) {
		(void)cursor->c_close(cursor);
		t->db->err(t->db, dbret, "tracking_printall dberror(get):");
		return RET_DBERR(dbret);
	}
	if( (dbret=cursor->c_close(cursor)) != 0 ) {
		t->db->err(t->db, dbret, "tracking_printall dberror(close):");
		return RET_DBERR(dbret);
	}
	return r;
}
	
retvalue tracking_parse(/*@null@*//*@only@*/char *option,struct distribution *d) {
	/*@temp@*/char *p,*q;

	if( option == NULL ) {
		d->tracking = dt_NONE;
		return RET_OK;
	}
	d->tracking = dt_NONE;
	q = option;
	while( *q != '\0') {
		p = q;
		while( *p != '\0' && xisspace(*p) ) {
			p++;
		}
		q = p;
		while( *q != '\0' && !xisspace(*q) ) {
			q++;
		}
		while( *q != '\0' && xisspace(*q) ) {
			*(q++) = '\0';
		}
		if( strcasecmp(p,"keep") == 0 ) {
			if( d->tracking != dt_NONE ) {
				fprintf(stderr,"Error in %s: Only one of 'keep','all' or 'minimal' can be in one Tracking: line.\n",d->codename);
				return RET_ERROR;
			}
			d->tracking = dt_KEEP;
		} else if( strcasecmp(p,"all") == 0 ) {
			fprintf(stderr,"Warning(%s): Tracking 'all' not yet supported.\n",d->codename);
			if( d->tracking != dt_NONE ) {
				fprintf(stderr,"Error in %s: Only one of 'keep','all' or 'minimal' can be in one Tracking: line.\n",d->codename);
				return RET_ERROR;
			}
			d->tracking = dt_ALL;
		} else if( strcasecmp(p,"minimal") == 0 ) {
			fprintf(stderr,"Warning(%s): Tracking 'minimal' not yet supported.\n",d->codename);
			if( d->tracking != dt_NONE ) {
				fprintf(stderr,"Error in %s: Only one of 'keep','all' or 'minimal' can be in one Tracking: line.\n",d->codename);
				return RET_ERROR;
			}
			d->tracking = dt_MINIMAL;
		} else if( strcasecmp(p,"includechanges") == 0 ) {
			d->trackingoptions.includechanges = TRUE;
		} else if( strcasecmp(p,"includebyhand") == 0 ) {
			d->trackingoptions.includebyhand = TRUE;
		} else if( strcasecmp(p,"needsources") == 0 ) {
			fprintf(stderr,"Warning(%s): 'needsources' not yet supported.\n",d->codename);
			d->trackingoptions.needsources = TRUE;
		} else if( strcasecmp(p,"embargoalls") == 0 ) {
			fprintf(stderr,"Warning(%s): 'embargoalls' not yet supported.\n",d->codename);
			d->trackingoptions.embargoalls = TRUE;
		} else {
			fprintf(stderr,"Unsupported tracking option: '%s' in %s.\n",p,d->codename);
			return RET_ERROR;
		}
	}

	free(option);
	if( d->tracking == dt_NONE ) {
		fprintf(stderr,"Error in %s: There is a Tracking: line but none of 'keep','all' or 'minimal' was found there.\n",d->codename);
		return RET_ERROR;
	}
	return RET_OK;
}

retvalue trackingdata_remember(struct trackingdata *td,/*@only@*/char*name,/*@only@*/char*version) {
	struct trackingdata_remember *r;

	r = malloc(sizeof(*r));
	if( r == NULL )
		return RET_ERROR_OOM;
	r->name = name;
	r->version = version;
	r->next = td->remembered;
	td->remembered = r;
	return RET_OK;
}

retvalue trackingdata_summon(trackingdb tracks,const char *name,const char *version,struct trackingdata *data) {
	struct trackedpackage *pkg;
	retvalue r;

	r = tracking_get(tracks,name,version,&pkg);
	if( RET_IS_OK(r) ) {
		data->tracks = tracks;
		data->pkg = pkg;
		data->isnew = FALSE;
		return r;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	r = tracking_new(tracks,name,version,&pkg);
	if( RET_IS_OK(r) ) {
		data->tracks = tracks;
		data->pkg = pkg;
		data->isnew = TRUE;
		return r;
	}
	return r;
}

retvalue trackingdata_new(trackingdb tracks,struct trackingdata *data) {

	data->tracks = tracks;
	data->pkg = NULL;
	return RET_OK;
}

retvalue trackingdata_insert(struct trackingdata *data,enum filetype filetype,const struct strlist *filekeys,/*@null@*//*@only@*/char*oldsource,/*@null@*//*@only@*/char*oldversion,/*@null@*/const struct strlist *oldfilekeys, references refs) {
	retvalue result,r;
	struct trackedpackage *pkg;

	if( data == NULL) {
		assert(oldversion == NULL && oldsource == NULL);
		free(oldversion);
		free(oldsource);
		return RET_OK;
	}
	assert(data->pkg != NULL);
	result = trackedpackage_addfilekeys(data->tracks,data->pkg,filetype,filekeys,refs);
	if( RET_WAS_ERROR(result) ) {
		free(oldsource);free(oldversion);
		return result;
	}
	if( oldsource == NULL || oldversion == NULL || oldfilekeys == NULL ) {
		assert(oldsource==NULL&&oldversion==NULL&&oldfilekeys==NULL);
		return RET_OK;
	}
	if( strcmp(oldversion,data->pkg->sourceversion) == 0 &&
			strcmp(oldsource,data->pkg->sourcename) == 0 ) {
		/* Unlikely, but it may also be the same source version as
		 * the package we are currently adding */
		free(oldsource);free(oldversion);
		return trackedpackage_removefilekeys(data->tracks,data->pkg,oldfilekeys);
	}
	r = tracking_get(data->tracks,oldsource,oldversion,&pkg);
	if( RET_WAS_ERROR(r) ) {
		free(oldsource);free(oldversion);
		return r;
	}
	if( r == RET_NOTHING) {
		fprintf(stderr,"Could not found tracking data for %s_%s in %s to remove old files from it.\n",oldsource,oldversion,data->tracks->codename);
		free(oldsource);free(oldversion);
		return result;
	}
	r = trackedpackage_removefilekeys(data->tracks,pkg,oldfilekeys);
	RET_UPDATE(result,r);
	r = tracking_replace(data->tracks,pkg);
	RET_UPDATE(result,r);
	trackedpackage_free(pkg);
	r = trackingdata_remember(data,oldsource,oldversion);
	RET_UPDATE(result,r);

	return result;
}

retvalue trackingdata_remove(struct trackingdata *data,/*@only@*/char*oldsource,/*@only@*/char*oldversion,const struct strlist *oldfilekeys) {
	retvalue result,r;
	struct trackedpackage *pkg;

	assert(oldsource != NULL && oldversion != NULL && oldfilekeys != NULL);
	if( data->pkg != NULL &&
			strcmp(oldversion,data->pkg->sourceversion) == 0 &&
			strcmp(oldsource,data->pkg->sourcename) == 0 ) {
		/* Unlikely, but it may also be the same source version as
		 * the package we are currently adding */
		free(oldsource);free(oldversion);
		return trackedpackage_removefilekeys(data->tracks,data->pkg,oldfilekeys);
	}
	result = tracking_get(data->tracks,oldsource,oldversion,&pkg);
	if( RET_WAS_ERROR(result) ) {
		free(oldsource);free(oldversion);
		return result;
	}
	if( result == RET_NOTHING) {
		fprintf(stderr,"Could not found tracking data for %s_%s in %s to remove old files from it.\n",oldsource,oldversion,data->tracks->codename);
		free(oldsource);free(oldversion);
		return RET_OK;
	}
	r = trackedpackage_removefilekeys(data->tracks,pkg,oldfilekeys);
	RET_UPDATE(result,r);
	r = tracking_replace(data->tracks,pkg);
	RET_UPDATE(result,r);
	trackedpackage_free(pkg);
	r = trackingdata_remember(data,oldsource,oldversion);
	RET_UPDATE(result,r);

	return result;
}

void trackingdata_done(struct trackingdata *d) {
	trackedpackage_free(d->pkg);
	d->pkg = NULL;
	d->tracks = NULL;
}
