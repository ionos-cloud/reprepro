#ifndef REPREPRO_FORCE_H
#define REPREPRO_FORCE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif

#define VALID_IGNORES \
	IGN(ignore) \
	IGN(invalid_distribution) \
	IGN(forbiddenchar) \
	IGN(8bit) \
	IGN(emptyfilenamepart) \
	IGN(overlongcomments) \
	IGN(spaceonlyline) \
	IGN(malformedchunk) \
	IGN(shortkeyid) \
	IGN(unknownfield)


enum ignore { 
#define IGN(what) IGN_ ## what,
	VALID_IGNORES
#undef IGN

	IGN_COUNT
};

extern int ignored[IGN_COUNT];
extern bool_t ignore[IGN_COUNT];

#define IGNORING(ignoring,toignore,what,msg_fmt, ...) \
	({ 	fprintf(stderr, msg_fmt , ## __VA_ARGS__); \
		ignored[IGN_ ## what] ++; \
		if( ignore[IGN_ ## what] ) { \
			fputs(ignoring " as --ignore=" #what " given.\n",stderr); \
		} else { \
			fputs(toignore " use --ignore=" #what ".\n",stderr); \
		} \
		ignore[IGN_ ## what]; \
	})
#define IGNORABLE(what) ignore[IGN_ ## what]

#define RETURN_IF_ERROR_UNLESS_IGNORED(r,what,msg_fmt, ...) \
	if( RET_WAS_ERROR(r) && !ISIGNORED(what,msg_fmt, ## __VA_ARGS__) ) \
		return r;

void init_ignores(void);

retvalue add_ignore(const char *given);

#endif
