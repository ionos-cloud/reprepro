#ifndef __MIRRORER_CHECKINDEB_H
#define __MIRRORER_CHECKINDEB_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* Add <package> with filename <filekey> and chunk <chunk> (which
 * alreadycontains "Filename:"), add an reference to <referee> in 
 * <references> and overwrite/add it to <pkgs> removing
 * references to oldfilekey that will be fall out of it by this */

retvalue checkindeb_insert( DB *references,const char *referee,
		           DB *pkgs,
		const char *package, const char *chunk,
		const char *filekey, const char *oldfilekey);

struct debpackage {
	char *package,*version,*source,*arch;
	char *filekey;
	char *md5andsize;
	char *control;
};
/* read the data from a .deb, add Filename, Size and md5sum to the control-item */
retvalue deb_read(struct debpackage **pkg, const char *component, const char *filename);
void deb_free(struct debpackage *pkg);

#endif
