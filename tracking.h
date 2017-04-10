#ifndef REPREPRO_TRACKING_H
#define REPREPRO_TRACKING_H


#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_TRACKINGT_H
#include "trackingt.h"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif

retvalue tracking_parse(struct distribution *, struct configiterator *);

/* high-level retrack of the whole distribution */
retvalue tracking_retrack(struct distribution *, bool /*evenifnotstale*/);

retvalue tracking_initialize(/*@out@*/trackingdb *, struct distribution *, bool readonly);
retvalue tracking_done(trackingdb, struct distribution *distribution);
retvalue tracking_listdistributions(/*@out@*/struct strlist *);
retvalue tracking_drop(const char *);

retvalue tracking_reset(trackingdb);
retvalue tracking_rereference(struct distribution *);

retvalue trackedpackage_addfilekey(trackingdb, struct trackedpackage *, enum filetype, /*@only@*/char * /*filekey*/, bool /*used*/);
retvalue trackedpackage_adddupfilekeys(trackingdb, struct trackedpackage *, enum filetype, const struct strlist * /*filekeys*/, bool /*used*/);
retvalue trackedpackage_removefilekeys(trackingdb, struct trackedpackage *, const struct strlist *);
void trackedpackage_free(struct trackedpackage *);

retvalue tracking_get(trackingdb, const char * /*sourcename*/, const char * /*version*/, /*@out@*/struct trackedpackage **);
retvalue tracking_getornew(trackingdb, const char * /*name*/, const char * /*version*/, /*@out@*/struct trackedpackage **);
retvalue tracking_save(trackingdb, /*@only@*/struct trackedpackage *);
retvalue tracking_remove(trackingdb, const char * /*sourcename*/, const char * /*version*/);
retvalue tracking_printall(trackingdb);

retvalue trackingdata_summon(trackingdb, const char *, const char *, struct trackingdata *);
retvalue trackingdata_new(trackingdb, struct trackingdata *);
retvalue trackingdata_switch(struct trackingdata *, const char *, const char *);
struct package;
retvalue trackingdata_insert(struct trackingdata *, enum filetype, const struct strlist * /*filekeys*/, /*@null@*/const struct package * /*oldpackage*/, /*@null@*/const struct strlist * /*oldfilekeys*/);
retvalue trackingdata_remove(struct trackingdata *, const char */*oldsource*/, const char * /*oldversion*/, const struct strlist * /*filekeys*/);
void trackingdata_done(struct trackingdata *);
/* like _done but actually do something */
retvalue trackingdata_finish(trackingdb, struct trackingdata *);

/* look at all listed packages and remove everything not needed */
retvalue tracking_tidyall(trackingdb);

retvalue tracking_removepackages(trackingdb, struct distribution *, const char * /*sourcename*/, /*@null@*/const char * /*version*/);
#endif /*REPREPRO_TRACKING_H*/
