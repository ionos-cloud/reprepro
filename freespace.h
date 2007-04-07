#ifndef REPREPRO_FREESPACE_H
#define REPREPRO_FREESPACE_H

struct devices;

retvalue space_prepare(const char *dbdir,/*@out@*/struct devices **devices);

retvalue space_needed(/*@null@*/struct devices *,const char *filename,const char *md5sum);

retvalue space_check(/*@null@*/struct devices *);

void space_free(/*@only@*//*@null@*/struct devices *);

#endif
