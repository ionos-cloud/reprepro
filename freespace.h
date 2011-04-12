#ifndef REPREPRO_FREESPACE_H
#define REPREPRO_FREESPACE_H

#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif

struct devices;
enum spacecheckmode { scm_NONE, /* scm_ASSUMESINGLEFS, */ scm_FULL };

retvalue space_prepare(/*@out@*/struct devices **, enum spacecheckmode, off_t /*reservedfordb*/, off_t /*reservedforothers*/);

struct checksums;
retvalue space_needed(/*@null@*/struct devices *, const char * /*filename*/, const struct checksums *);

retvalue space_check(/*@null@*/struct devices *);

void space_free(/*@only@*//*@null@*/struct devices *);

#endif
