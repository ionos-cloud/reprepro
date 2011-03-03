#ifndef REPREPRO_GLOBALS_H
#define REPREPRO_GLOBALS_H

#define TRUE (1==1)
#define FALSE (0==42)
#ifdef AVOID_CHECKPROBLEMS
#define bool_t _Bool
/* avoid problems with __builtin_expect being long instead of boolean */
#define __builtin_expect(a,b) (a)
#define __builtin_constant_p(a) (__builtin_constant_p(a) != 0)
#else
typedef int bool_t;
#endif
#define xisspace(c) (isspace(c)!=0)
#define xisblank(c) (isblank(c)!=0)
#define xisdigit(c) (isdigit(c)!=0)

#define ISSET(a,b) (a&b)!=0
#define NOTSET(a,b) (a&b)==0

#ifdef STUPIDCC
#define IFSTUPIDCC(a) a
#else
#define IFSTUPIDCC(a)
#endif

#ifdef SPLINT
#define UNUSED(a) /*@unused@*/ a
#else
#ifndef NOUNUSEDATTRIBUTE
#define UNUSED(a) a __attribute((unused))
#else
#define UNUSED(a) a
#endif
#endif

enum config_option_owner { 	CONFIG_OWNER_DEFAULT=0,
				CONFIG_OWNER_FILE,
				CONFIG_OWNER_ENVIRONMENT,
		           	CONFIG_OWNER_CMDLINE};
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
