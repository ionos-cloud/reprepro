#ifndef __MIRRORER_CHECKINDSC_H
#define __MIRRORER_CHECKINDSC_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_DISTRIBUTION_H
#include "distribution.h"
#endif

struct dscpackage {
	/* things to be set by dsc_read: */
	char *package,*version;
	char *control;
	/* things that might still be NULL then: */
	char *section;
	char *priority;
	/* things that will still be NULL then: */
	char *directory;
	char *component; //This might be const, too and save some strdups, but...
};

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <d->architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if component is NULL, guessing it from the section. */

retvalue dsc_add(const char *dbdir,DB *references,DB *filesdb,const char *mirrordir,const char *forcecomponent,const char *forcedsection,const char *forcepriority,struct distribution *distribution,const char *dscfilename,int force);

#endif
