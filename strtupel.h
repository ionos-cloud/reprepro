#ifndef __MIRRORER_STRTUPEL_H
#define __MIRRORER_STRTUPEL_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

typedef char strtupel;

const char *strtupel_get(const strtupel *tupel,int index);
strtupel *strtupel_fromarrays(int count,const char **strings,const size_t *lengths);
strtupel *strtupel_fromvalues(int count,...);

size_t strtupel_len(const strtupel *tupel);

retvalue strtupel_print(FILE *file,const strtupel *tupel);

#endif
