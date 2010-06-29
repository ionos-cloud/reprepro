#ifndef REPREPRO_FORCE_H
#define REPREPRO_FORCE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#endif

#define VALID_IGNORES \
	IGN(ignore) \
	IGN(forbiddenchar) \
	IGN(8bit) \
	IGN(emptyfilenamepart) \
	IGN(spaceonlyline) \
	IGN(malformedchunk) \
	IGN(unknownfield) \
	IGN(wrongdistribution) \
	IGN(missingfield) \
	IGN(brokenold) \
	IGN(brokenversioncmp) \
	IGN(extension) \
	IGN(unusedarch) \
	IGN(surprisingarch) \
	IGN(surprisingbinary) \
	IGN(wrongsourceversion) \
	IGN(wrongversion) \
	IGN(dscinbinnmu) \
	IGN(brokensignatures) \
	IGN(uploaders) \
	IGN(undefinedtarget) \
	IGN(undefinedtracking) \
	IGN(unusedoption) \
	IGN(flatandnonflat) \
	IGN(expiredkey) \
	IGN(revokedkey) \
	IGN(expiredsignature) \
	IGN(wrongarchitecture) \
	IGN(missingfile)


enum ignore {
#define IGN(what) IGN_ ## what,
	VALID_IGNORES
#undef IGN

	IGN_COUNT
};

extern int ignored[IGN_COUNT];
extern bool ignore[IGN_COUNT];

#define IGNORING(ignoring,toignore,what,...) \
	({ 	fprintf(stderr, ## __VA_ARGS__); \
		ignored[IGN_ ## what] ++; \
		if( ignore[IGN_ ## what] ) { \
			fputs(ignoring " as --ignore=" #what " given.\n",stderr); \
		} else { \
			fputs(toignore " use --ignore=" #what ".\n",stderr); \
		} \
		ignore[IGN_ ## what]; \
	})
#define IGNORING_(what,...) IGNORING("Ignoring","To ignore",what, __VA_ARGS__ )
#define IGNORABLE(what) ignore[IGN_ ## what]

#define RETURN_IF_ERROR_UNLESS_IGNORED(r,what,msg_fmt, ...) \
	if( RET_WAS_ERROR(r) && !ISIGNORED(what,msg_fmt, ## __VA_ARGS__) ) \
		return r;

void init_ignores(void);

retvalue set_ignore(const char *given, bool newvalue, enum config_option_owner newowner);

#endif
