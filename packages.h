#ifndef REPREPRO_PACKAGES_H
#define REPREPRO_PACKAGES_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_NAMES_H
#include "names.h"
#endif
#ifndef REPREPRO_FILES_H
#include "files.h"
#endif
#ifndef REPREPRO_REFERENCE_H
#include "reference.h"
#endif

typedef struct s_packagesdb *packagesdb;

/* initialize the packages-database for <identifier> */
retvalue packages_initialize(/*@out@*/packagesdb *pkgs,const char *dbpath,const char *identifier);

/* The same but calculate the identifier */
retvalue packages_init(packagesdb *pkgs,const char *dbpath,const char *codename,const char *component,const char *architecture,const char *packagetype);

/* release the packages-database initialized got be packages_initialize */
retvalue packages_done(/*@only@*/packagesdb db);

/* save a given chunk in the database */
//retvalue packages_add(packagesdb db,const char *package,const char *chunk);
/* replace a save chunk with another */
//retvalue packages_replace(packagesdb db,const char *package,const char *chunk);
/* remove a given chunk from the database */
retvalue packages_remove(packagesdb db,const char *package);
/* get the saved chunk from the database,
 * returns RET_NOTHING, if there is none*/
retvalue packages_get(packagesdb db,const char *package,/*@out@*/char **chunk);

/* insert a chunk in the packages database, adding and deleting
 * references and insert files while that.
 * free oldfiles, if != NULL, add filekeys losing reference to derferencedfilekeys*/
retvalue packages_insert(references refs, packagesdb packagesdb,
		const char *packagename, const char *controlchunk,
		const struct strlist *files,
		struct strlist *oldfiles,
		struct strlist *dereferencedfilekeys);

retvalue packages_export(packagesdb packagesdb,const char *filename,indexcompression compression);

/* action to be called by packages_forall */
typedef retvalue per_package_action(void *data,const char *package,/*@temp@*/const char *chunk);

/* call action once for each saved chunk: */
retvalue packages_foreach(packagesdb packagesdb,per_package_action action,/*@temp@*/ /*@null@*/void *data, int force);

#endif
