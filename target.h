#ifndef __MIRRORER_TARGET_H
#define __MIRRORER_TARGET_H

#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#endif
#ifndef __MIRRORER_NAMES_H
#include "names.h"
#endif
#ifndef __MIRRORER_PACKAGES_H
#include "packages.h"
#endif

typedef struct s_target *target;

typedef retvalue get_name(target,const char *,char **);
typedef retvalue get_version(target,const char *,char **);
typedef retvalue get_installdata(target,const char *,const char *,const char *,char **,struct strlist *,struct strlist *,struct strlist *);
typedef retvalue get_filekeys(target,const char *,const char *,struct strlist *);

struct s_target {
	char *codename;
	char *component;
	char *architecture;
	char *identifier;
	char *directory;
	int compressions[ic_max+1];
	const char *indexfile;
	get_name *getname;
	get_version *getversion;
	get_installdata *getinstalldata;
	get_filekeys *getfilekeys;
	/* the next one in the list of targets of a distribution */
	struct s_target *next;
	/* is initialized as soon as needed: */
	packagesdb packages;
};

retvalue target_initialize_binary(const char *codename,const char *component,const char *architecture,target *target);
retvalue target_initialize_source(const char *codename,const char *component,target *target);
void target_free(target target);


retvalue target_printmd5sums(target target,const char *distdir,FILE *out,int force);

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(target target, const char *dbdir, packagesdb *db);

/* The following calls can only be called if target_initpackagesdb was called before: */

retvalue target_addpackage(target target,DB *references,filesdb files,const char *name,const char *version,const char *control,const struct strlist *filekeys,const struct strlist *md5sums,int force,int downgrade);
retvalue target_export(target target,const char *distdir, int force);
retvalue target_check(target target,filesdb filesdb,DB *referencesdb,int force);
retvalue target_rereference(target target,DB *referencesdb,int force);

#endif
