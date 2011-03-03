#ifndef REPREPRO_GLOBALS_H
#define REPREPRO_GLOBALS_H

#define TRUE (1==1)
#define FALSE (0==42)
#ifdef AVOID_CHECKPROBLEMS
#define bool_t _Bool
/* avoid problems with __builtin_expect beeing long instead of boolean */
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

#endif
