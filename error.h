#ifndef REPREPRO_ERROR_H
#define REPREPRO_ERROR_H

#ifndef REPREPRO_GLOBALS_H
#include "globals.h"
#endif

bool interrupted(void);

/* retvalue is simply an int.
 * just named to show it follows the given semantics */
/*@numabstract@*/ enum retvalue_enum {
	RET_ERROR_INCOMING_DENY = -13,
	RET_ERROR_INTERNAL = -12,
	RET_ERROR_BZ2 = -11,
	RET_ERROR_Z = -10,
	RET_ERROR_INTERRUPTED = -9,
	RET_ERROR_UNKNOWNFIELD = -8,
	RET_ERROR_MISSING = -7,
	RET_ERROR_BADSIG = -6,
	RET_ERROR_GPGME = -5,
	RET_ERROR_EXIST = -4,
	RET_ERROR_OOM = -3,
	RET_ERROR_WRONG_MD5 = -2,
	RET_ERROR = -1,
	RET_NOTHING = 0,
	RET_OK  = 1
};
typedef enum retvalue_enum retvalue;

#define FAILEDTOALLOC(x) unlikely(x == NULL)

#define RET_IS_OK(r) likely((r) == RET_OK)
#define RET_WAS_NO_ERROR(r) likely((r) >= (retvalue)0)
#define RET_WAS_ERROR(r) unlikely((r) < (retvalue)0)

/* update a return value, so that it contains the first error-code
 * and otherwise is RET_OK, if anything was RET_OK */
#define RET_UPDATE(ret, update) { if ((update)!=RET_NOTHING && RET_WAS_NO_ERROR(ret)) ret=update;}

/* like RET_UPDATE, but RET_ENDUPDATE(RET_NOTHING, RET_OK) keeps RET_NOTHING */
#define RET_ENDUPDATE(ret, update) {if (RET_WAS_ERROR(update) && RET_WAS_NO_ERROR(ret)) ret=update;}

/* code a errno in a error */
#define RET_ERRNO(err) ((err>0)?((retvalue)-err):RET_ERROR)

/* code a db-error in a error */
// TODO: to be implemented...
#define RET_DBERR(e) RET_ERROR

#define ASSERT_NOT_NOTHING(r) {assert (r != RET_NOTHING); if (r == RET_NOTHING) r = RET_ERROR_INTERNAL;}

#define EXIT_RET(ret) (RET_WAS_NO_ERROR(ret)?((nothingiserror&&ret==RET_NOTHING)?EXIT_FAILURE:EXIT_SUCCESS):(int)ret)

#endif
