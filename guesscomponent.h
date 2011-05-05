#ifndef REPREPRO_GUESSCOMPONENT_H
#define REPREPRO_GUESSCOMPONENT_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif

retvalue guess_component(const char * /*codename*/, const struct atomlist * /*components*/, const char * /*package*/, const char * /*section*/, component_t, /*@out@*/component_t *);

#endif
