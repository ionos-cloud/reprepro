#ifndef __MIRRORER_GUESSCOMPONENT_H
#define __MIRRORER_GUESSCOMPONENT_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef __MIRRORER_STRLIST_H
#include "strlist.h"
#warning "What's hapening here?"
#endif

retvalue guess_component(const char *codename,const struct strlist *components,
			const char *package,const char *section,
			const char *givencomponent,char **component);

#endif
