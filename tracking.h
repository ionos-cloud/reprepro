#ifndef REPREPRO_TRACKING_H
#define REPREPRO_TRACKING_H

typedef struct s_tracking *trackingdb;

#ifndef REPREPRO_REFERENCE_H
#include "reference.h"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif



retvalue tracking_parse(/*@null@*//*@only@*/char *option,struct distribution *);

retvalue tracking_initialize(/*@out@*/trackingdb *,const char *dbpath,const struct distribution *);
retvalue tracking_done(trackingdb);


struct trackedpackage {
	char *sourcename;
	char *sourceversion;
/*	char *sourcedir; */
	struct strlist filekeys;
	int *refcounts;
};

enum filetype { ft_ALL_BINARY='a',
		ft_ARCH_BINARY='b', 
		ft_CHANGES = 'c',  
		ft_SOURCE='s',
		ft_XTRA_DATA='x'};

retvalue trackedpackage_addfilekey(trackingdb,struct trackedpackage *,enum filetype,const char *filekey,references);
retvalue trackedpackage_addfilekeys(trackingdb,struct trackedpackage *,enum filetype,const struct strlist *filekeys,references);
void trackedpackage_free(struct trackedpackage *pkg);

retvalue tracking_get(trackingdb,const char *name,const char *version,/*@out@*/struct trackedpackage **);
retvalue tracking_new(trackingdb,const char *name,const char *version,/*@out@*/struct trackedpackage **);
retvalue tracking_put(trackingdb,struct trackedpackage *);
retvalue tracking_replace(trackingdb,struct trackedpackage *);
retvalue tracking_remove(trackingdb,const char *sourcename,const char *version,references,/*@null@*/struct strlist *unreferencedfilekeys);
retvalue tracking_clearall(trackingdb);
retvalue tracking_printall(trackingdb t);

#endif /*REPREPRO_TRACKING_H*/
