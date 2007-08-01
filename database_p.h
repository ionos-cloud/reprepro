#ifndef REPREPRO_DATABASE_P_H
#define REPREPRO_DATABASE_P_H

#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

struct references;
struct filesdb;

struct database {
	char *directory;
	struct filesdb *files;
	struct references *references;
	bool_t locked;
};

#if LIBDB_VERSION == 44
#define DB_OPEN(database,filename,name,type,flags) database->open(database,NULL,filename,name,type,flags,0664)
#elif LIBDB_VERSION == 43
#define DB_OPEN(database,filename,name,type,flags) database->open(database,NULL,filename,name,type,flags,0664)
#else
#if LIBDB_VERSION == 3
#define DB_OPEN(database,filename,name,type,flags) database->open(database,filename,name,type,flags,0664)
#else
#error Unexpected LIBDB_VERSION!
#endif
#endif

#endif
