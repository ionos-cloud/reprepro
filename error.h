#ifndef __MIRRORER_ERROR_H
#define __MIRRORER_ERROR_H

/* retvalue is simply an int.
 * just named to show it follows the given semantics */
typedef int retvalue;

#define RET_OK 1
#define RET_NOTHING 0
#define RET_ERROR -1
#define RET_ERROR_WRONG_MD5 -2
#define RET_ERROR_OOM -3
#define RET_ERROR_EXIST -4

#define RET_IS_OK(r) ((r) == RET_OK)
#define RET_WAS_NO_ERROR(r) ((r) >= 0)
#define RET_WAS_ERROR(r) ((r) < 0)

/* update a return value, so that it contains the first error-code
 * and otherwise is RET_OK, if anything was RET_OK */
#define RET_UPDATE(ret,update) { if( (update)!=0 && (ret)>=0 ) ret=update;}

/* like RET_UPDATE, but RET_ENDUPDATE(RET_NOTHING,RET_OK) keeps RET_NOTHING */
#define RET_ENDUPDATE(ret,update) { if( (update)<0 && (ret)>=0 ) ret=update;}

/* code a errno in a error */
#define RET_ERRNO(err) ((err>0)?(-err):RET_ERROR)

/* code a zlib-error in a error */
#define RET_ZERRNO(zerr) ((zerr>=Z_OK)?RET_OK:((zerr==Z_ERRNO)?RET_ERRNO(errno):(-5000+zerr)))

/* code a db-error in a error */
// TODO: to be implemented...
#define RET_DBERR(e) RET_ERROR

#define EXIT_RET(ret) ((ret>=0)?((nothingiserror&&ret==0)?1:0):ret)

#endif
