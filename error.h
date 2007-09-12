#ifndef REPREPRO_ERROR_H
#define REPREPRO_ERROR_H

#ifndef REPREPRO_GLOBALS_H
#include "globals.h"
#endif

bool interrupted(void);

/* retvalue is simply an int.
 * just named to show it follows the given semantics */
typedef int retvalue;

#define RET_OK (retvalue)1
#define RET_NOTHING (retvalue)0
#define RET_ERROR (retvalue)-1
#define RET_ERROR_WRONG_MD5 (retvalue)-2
#define RET_ERROR_OOM (retvalue)-3
#define RET_ERROR_EXIST (retvalue)-4
#define RET_ERROR_GPGME (retvalue)-5
#define RET_ERROR_BADSIG (retvalue)-6
#define RET_ERROR_MISSING (retvalue)-7
#define RET_ERROR_UNKNOWNFIELD (retvalue)-8
#define RET_ERROR_INTERRUPTED (retvalue)-9

#define RET_IS_OK(r) ((r) == RET_OK)
#define RET_WAS_NO_ERROR(r) ((r) >= (retvalue)0)
#define RET_WAS_ERROR(r) ((r) < (retvalue)0)

/* update a return value, so that it contains the first error-code
 * and otherwise is RET_OK, if anything was RET_OK */
#define RET_UPDATE(ret,update) { if( (update)!=RET_NOTHING && RET_WAS_NO_ERROR(ret) ) ret=update;}

/* like RET_UPDATE, but RET_ENDUPDATE(RET_NOTHING,RET_OK) keeps RET_NOTHING */
#define RET_ENDUPDATE(ret,update) {if(RET_WAS_ERROR(update)&&RET_WAS_NO_ERROR(ret)) ret=update;}

/* code a errno in a error */
#define RET_ERRNO(err) ((err>0)?((retvalue)-err):RET_ERROR)

/* code a zlib-error in a error */
#define RET_ZERRNO(zerr) ((zerr>=Z_OK)?RET_OK:((zerr==Z_ERRNO)?RET_ERRNO(errno):((retvalue)(-5000+zerr))))

/* code a db-error in a error */
// TODO: to be implemented...
#define RET_DBERR(e) RET_ERROR

#define EXIT_RET(ret) (RET_WAS_NO_ERROR(ret)?((nothingiserror&&ret==RET_NOTHING)?EXIT_FAILURE:EXIT_SUCCESS):(int)ret)

#endif
