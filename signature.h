#ifndef __MIRRORER_SIGNATURE_H
#define __MIRRORER_SIGNATURE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

retvalue signature_check(const char *chunk, const char *releasegpg, const char *release);

#endif
