#ifndef REPREPRO_FREESPACE_H
#define REPREPRO_FREESPACE_H

struct devices;
enum spacecheckmode { scm_NONE, /* scm_ASSUMESINGLEFS, */ scm_FULL };

retvalue space_prepare(const char *dbdir,/*@out@*/struct devices **,enum spacecheckmode,off_t reservedfordb,off_t reservedforothers);

retvalue space_needed(/*@null@*/struct devices *,const char *filename,const char *md5sum);

retvalue space_check(/*@null@*/struct devices *);

void space_free(/*@only@*//*@null@*/struct devices *);

#endif
