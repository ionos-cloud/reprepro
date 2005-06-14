#ifndef REPREPRO_TRACKINGT_H
#define REPREPRO_TRACKINGT_H

#ifndef REPREPRO_REFERENCE_H
#include "reference.h"
#endif

struct trackedpackage {
	char *sourcename;
	char *sourceversion;
/*	char *sourcedir; */
	struct strlist filekeys;
	int *refcounts;
};
typedef struct s_tracking *trackingdb;

enum filetype { ft_ALL_BINARY='a',
		ft_ARCH_BINARY='b', 
		ft_CHANGES = 'c',  
		ft_SOURCE='s',
		ft_XTRA_DATA='x'};

struct trackingdata {
	/*@temp@*/trackingdb tracks;
	struct trackedpackage *pkg;
	bool_t isnew;
	/*@null@*/ struct trackingdata_remember {
		/*@null@*/struct trackingdata_remember *next;
		char *name;
		char *version;
	} *remembered;
};
#endif /*REPREPRO_TRACKINGT_H*/
