#ifndef REPREPRO_OUTHOOK_H
#define REPREPRO_OUTHOOK_H

#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif

retvalue outhook_start(void);
void outhook_send(const char *, const char *, const char *, const char *);
void outhook_sendpool(component_t, const char *, const char *);
retvalue outhook_call(const char *);

#endif
