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
	IGN(oldfile) \
	IGN(longkeyid) \
	IGN(missingfile)


enum ignore {
#define IGN(what) IGN_ ## what,
	VALID_IGNORES
#undef IGN

	IGN_COUNT
};

extern int ignored[IGN_COUNT];
extern bool ignore[IGN_COUNT];

/* Having that as function avoids those strings to be duplacated everywhere */
bool print_ignore_type_message(bool, enum ignore);

#define IGNORING__(ignoring, what, ...) ({ \
 	fprintf(stderr, ## __VA_ARGS__); \
 	print_ignore_type_message(ignoring, IGN_ ## what ); \
})
#define IGNORING(what, ...) IGNORING__(true, what, __VA_ARGS__)
#define IGNORING_(what, ...) IGNORING__(false, what, __VA_ARGS__)
#define IGNORABLE(what) ignore[IGN_ ## what]

retvalue set_ignore(const char *, bool, enum config_option_owner);

#endif
