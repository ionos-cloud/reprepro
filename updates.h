#ifndef __MIRRORER_UPDATES_H
#define __MIRRORER_UPDATES_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

#include "release.h"
#include "strlist.h"


struct update {
	char *name;
	struct strlist architectures;
	char *suite_from;
	struct strlist components_from;
	struct strlist components_into;
};

typedef retvalue updatesaction(void *data,const char *chunk,const struct distribution *distribution,struct update *update);

retvalue updates_foreach(const char *confdir,int argc,char *argv[],updatesaction action,void *data,int force);

/* Add to todownload (which should already be initialized) all indixes to get
 * (like Packages.gz Sources.gz Release and Release.gpg) */
retvalue updates_calcliststofetch(struct strlist *todownload,
		/* where to save to file */
		const char *listdir, const char *codename,const char *update,const char *updateschunk,
		/* where to get it from */
		const char *suite_from,
		/* what parts to get */
		const struct strlist *components_from,
		const struct strlist *architectures
		);

/* Check for the files that calcListsToFetch asked to download were downloaded correctly and
 * are correctly signed. (If the current fields in the updatechunk are available/missing) */
retvalue updates_checkfetchedlists(const struct update *update,const char *updatechunk,const char *listdir,const char *codename);

#endif
