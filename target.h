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
};

retvalue target_initialize_binary(const char *distribution,const char *component,const char *architecture,target *target);
retvalue target_initialize_source(const char *distribution,const char *component,target *target);
void target_done(target target);

retvalue target_addpackage(target target,packagesdb packages,DB *references,filesdb files,const char *name,const char *version,const char *control,const struct strlist *filekeys,const struct strlist *md5sums,int force);
retvalue target_rereference(const char *dbdir,DB *referencesdb,target target,int force);
retvalue target_check(const char *dbdir,filesdb filesdb,DB *referencesdb,target target,int force);
retvalue target_export(target target,packagesdb packages,const char *distdir, int force);
retvalue target_doexport(target target,const char *dbdir,const char *distdir, int force);
retvalue target_printmd5sums(target target,const char *distdir,FILE *out,int force);
#endif
