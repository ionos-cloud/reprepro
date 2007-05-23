#ifndef REPREPRO_TRACKING_H
#define REPREPRO_TRACKING_H


#ifndef REPREPRO_REFERENCE_H
#include "reference.h"
#endif
#ifndef REPREPRO_TRACKINGT_H
#include "trackingt.h"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif



retvalue tracking_parse(/*@null@*//*@only@*/char *option,struct distribution *);

retvalue tracking_initialize(/*@out@*/trackingdb *,const char *dbpath,const struct distribution *);
retvalue tracking_done(trackingdb);

retvalue trackedpackage_addfilekey(trackingdb,struct trackedpackage *,enum filetype,/*@only@*/char *filekey,bool_t used,references);
retvalue trackedpackage_addfilekeys(trackingdb,struct trackedpackage *,enum filetype,struct strlist *filekeys,bool_t used,references);
retvalue trackedpackage_adddupfilekeys(trackingdb,struct trackedpackage *,enum filetype,const struct strlist *filekeys,bool_t used,references);
retvalue trackedpackage_removefilekeys(trackingdb,struct trackedpackage *,const struct strlist *);
void trackedpackage_free(struct trackedpackage *pkg);

retvalue tracking_getornew(trackingdb,const char *name,const char *version,/*@out@*/struct trackedpackage **);
retvalue tracking_save(trackingdb,/*@only@*/struct trackedpackage *);
retvalue tracking_remove(trackingdb,const char *sourcename,const char *version,references,/*@null@*/struct strlist *unreferencedfilekeys);
retvalue tracking_removeall(trackingdb);
retvalue tracking_printall(trackingdb t);

retvalue trackingdata_summon(trackingdb,const char*,const char*,struct trackingdata *);
retvalue trackingdata_new(trackingdb,struct trackingdata *);
retvalue trackingdata_insert(struct trackingdata *,enum filetype,const struct strlist *filekeys,/*@null@*//*@only@*/char*oldsource,/*@null@*//*@only@*/char*oldversion,/*@null@*/const struct strlist *oldfilekeys, references);
retvalue trackingdata_remove(struct trackingdata *,/*@only@*/char*oldsource,/*@only@*/char*oldversion,const struct strlist *filekeys);
void trackingdata_done(struct trackingdata *);
/* like _done but actually do something */
retvalue trackingdata_finish(trackingdb tracks, struct trackingdata *d, references refs, struct strlist *dereferenced);

/* look at all listed packages and remove everything not needed */
retvalue tracking_tidyall(trackingdb, references, struct strlist *dereferenced);
#endif /*REPREPRO_TRACKING_H*/
