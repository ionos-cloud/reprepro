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

DB *initialize_packages(const char *dbpath,const char *identifier) {
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


int replacepackage(DB *packagesdb,const char *package,const char *chunk) {
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

int addpackage(DB *packagesdb,const char *package,const char *chunk) {
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
char *getpackage(DB *packagesdb,const char *package) {
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

int removepackage(DB *packagesdb,const char *package) {
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

int checkpackage(DB *packagesdb,const char *package) {
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

int packages_printout(DB *packagesdb,const char *filename) {
	DBC *cursor;
	DBT key,data;
	int ret;
	FILE *pf;

	cursor = NULL;
	if( (ret = packagesdb->cursor(packagesdb,NULL,&cursor,0)) != 0 ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}
	CLEARDBT(key);	
	CLEARDBT(data);	


	pf = fopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		return -1;
	}
	
	while( (ret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
//		fprintf(pf,"%s\n",(const char*)data.data);		
		fwrite(data.data,strlen(data.data),1,pf);
		fwrite("\n",1,1,pf);
	}

	fclose(pf);
	
	if( ret != DB_NOTFOUND ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}
	if( (ret = cursor->c_close(cursor)) != 0 ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}

	return 0;
}

int packages_zprintout(DB *packagesdb,const char *filename) {
	DBC *cursor;
	DBT key,data;
	int ret;
	gzFile pf;

	cursor = NULL;
	if( (ret = packagesdb->cursor(packagesdb,NULL,&cursor,0)) != 0 ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}
	CLEARDBT(key);	
	CLEARDBT(data);	


	pf = gzopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		return -1;
	}
	
	while( (ret=cursor->c_get(cursor,&key,&data,DB_NEXT)) == 0 ) {
		gzwrite(pf,data.data,strlen(data.data));
		gzwrite(pf,"\n",1);
	}

	gzclose(pf);
	
	if( ret != DB_NOTFOUND ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}
	if( (ret = cursor->c_close(cursor)) != 0 ) {
		packagesdb->err(packagesdb, ret, "packages.db:");
		return -1;
	}

	return 0;
}
