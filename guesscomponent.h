#ifndef REPREPRO_GUESSCOMPONENT_H
#define REPREPRO_GUESSCOMPONENT_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#warning "What's hapening here?"
#endif

retvalue guess_component(const char *codename,const struct strlist *components,
			const char *package,const char *section,
			const char *givencomponent,char **component);

#endif
