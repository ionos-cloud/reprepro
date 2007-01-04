#ifndef REPREPRO_INCOMING_H
#define REPREPRO_INCOMING_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

retvalue process_incoming(const char *basedir,const char *confdir, filesdb files, const char *dbdir, references refs, struct strlist *dereferenced, struct distribution *distributions, const char *name);
#endif
