#ifndef __MIRRORER_UPDATES_H
#define __MIRRORER_UPDATES_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

#include "release.h"
#include "strlist.h"

typedef retvalue updatesaction(void *data,const char *chunk,const struct release *release,const char *name);

retvalue updates_foreach_matching(const char *conf,const struct release *release,const struct strlist *updates,updatesaction action,void *data,int force);



#endif
