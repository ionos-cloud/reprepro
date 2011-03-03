#ifndef REPREPRO_CHECKS_H
#define REPREPRO_CHECKS_H

/* return NULL if no problem, statically allocated string otherwise */

typedef const char *checkfunc(const char *);

const char *checkfordirectoryandidentifier(const char *);
#define checkforcomponent checkfordirectoryandidentifier
#define checkforcodename checkfordirectoryandidentifier
const char *checkforidentifierpart(const char *);
#define checkforarchitecture checkforidentifierpart

/* not yet used */
static inline void checkerror_free(UNUSED(const char *dummy)) {};
#endif
