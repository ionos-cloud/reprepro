#ifndef __MIRRORER_DOWNLOAD_H
#define __MIRRORER_DOWNLOAD_H

#include <db.h>
#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#warning "What's hapening here?"
#endif

struct download;

retvalue download_initialize(struct download **download,const char *method,const char *config);
retvalue download_add(struct download *download,const char *orig,const char *dest);
retvalue download_run(struct download *download);
retvalue download_cancel(struct download *download);

retvalue download_fetchfiles(const char *method,const char *config, 
		             struct strlist *todownload);
#endif
