#ifndef REPREPRO_INCOMING_H
#define REPREPRO_INCOMING_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

retvalue process_incoming(struct distribution *distributions, const char *name, /*@null@*/const char *onlychangesfilename);
#endif
