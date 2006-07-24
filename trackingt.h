#ifndef REPREPRO_TRACKINGT_H
#define REPREPRO_TRACKINGT_H

#ifndef REPREPRO_REFERENCE_H
#include "reference.h"
#endif

enum filetype { ft_ALL_BINARY='a',
		ft_ARCH_BINARY='b', 
		ft_CHANGES = 'c',  
		ft_SOURCE='s',
		ft_XTRA_DATA='x'};

struct trackedpackage {
	char *sourcename;
	char *sourceversion;
	struct strlist filekeys;
	int *refcounts;
	enum filetype *filetypes;
	struct {
		bool_t isnew:1;
		bool_t deleted:1;
	} flags;
};
typedef struct s_tracking *trackingdb;

struct trackingdata {
	/*@temp@*/trackingdb tracks;
	struct trackedpackage *pkg;
	/*@null@*/ struct trackingdata_remember {
		/*@null@*/struct trackingdata_remember *next;
		char *name;
		char *version;
	} *remembered;
};
#endif /*REPREPRO_TRACKINGT_H*/
