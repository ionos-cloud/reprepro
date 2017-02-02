#ifndef REPREPRO_GLOBALS_H
#define REPREPRO_GLOBALS_H

#include <string.h>

#ifdef AVOID_CHECKPROBLEMS
# define bool _Bool
# define true (1==1)
# define false (0==42)
/* avoid problems with __builtin_expect being long instead of boolean */
# define __builtin_expect(a, b) (a)
# define __builtin_constant_p(a) (__builtin_constant_p(a) != 0)
#else
# if HAVE_STDBOOL_H
#  include <stdbool.h>
# else
#  if ! HAVE__BOOL
typedef int _Bool;
#  endif
#  define true (1==1)
#  define false (0==42)
# endif
#endif

#define xisspace(c) (isspace(c)!=0)
#define xisblank(c) (isblank(c)!=0)
#define xisdigit(c) (isdigit(c)!=0)

#define READONLY true
#define READWRITE false

#define ISSET(a, b) ((a & b) != 0)
#define NOTSET(a, b) ((a & b) == 0)

/* sometimes something is initializes though the value is never used to
 * work around some gcc uninitialized-use false-positives */
#define SETBUTNOTUSED(a) a

#ifdef SPLINT
#define UNUSED(a) /*@unused@*/ a
#define NORETURN
#define likely(a) (a)
#define unlikely(a) (a)
#else
#define likely(a) (!(__builtin_expect(!(a), false)))
#define unlikely(a) __builtin_expect(a, false)
#define NORETURN __attribute((noreturn))
#ifndef NOUNUSEDATTRIBUTE
#define UNUSED(a) a __attribute((unused))
#else
#define UNUSED(a) a
#endif
#endif

#define ARRAYCOUNT(a) (sizeof(a)/sizeof(a[0]))

enum config_option_owner { 	CONFIG_OWNER_DEFAULT=0,
				CONFIG_OWNER_FILE,
				CONFIG_OWNER_ENVIRONMENT,
		           	CONFIG_OWNER_CMDLINE};
#ifndef _D_EXACT_NAMLEN
#define _D_EXACT_NAMLEN(r) (strlen((r)->d_name))
#endif
/* for systems defining NULL to 0 instead of the nicer (void*)0 */
#define ENDOFARGUMENTS ((char *)0)

/* global information */
extern int verbose;
extern struct global_config {
	const char *basedir;
	const char *dbdir;
	const char *outdir;
	const char *distdir;
	const char *confdir;
	const char *methoddir;
	const char *logdir;
	const char *listdir;
	const char *morguedir;
	/* flags: */
	bool keepdirectories;
	bool keeptemporaries;
	bool onlysmalldeletes;
	/* verbosity of downloading statistics */
	int showdownloadpercent;
} global;

enum compression { c_none, c_gzip, c_bzip2, c_lzma, c_xz, c_lunzip, c_zstd, c_COUNT };

#define setzero(type, pointer) ({type *__var = pointer; memset(__var, 0, sizeof(type));})
#define NEW(type) ((type *)malloc(sizeof(type)))
#define nNEW(num, type) ((type *)malloc((num) * sizeof(type)))
#define zNEW(type) ((type *)calloc(1, sizeof(type)))
#define nzNEW(num, type) ((type *)calloc(num, sizeof(type)))
#define arrayinsert(type, array, position, length) ({type *__var = array; memmove(__var + (position) + 1, __var + (position), sizeof(type) * ((length) - (position)));})

// strcmp2 behaves like strcmp, but allows both strings to be NULL
inline int strcmp2(const char *s1, const char *s2) {
	if (s1 == NULL || s2 == NULL) {
		if (s1 == NULL && s2 == NULL) {
			return 0;
		} else if (s1 == NULL) {
			return -1;
		} else {
			return 1;
		}
	} else {
		return strcmp(s1, s2);
	}
}

#endif
