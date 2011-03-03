#ifndef REPREPRO_NEEDBUILD_H
#define REPREPRO_NEEDBUILD_H

#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_DISTRIBUTION_H
#include "distribution.h"
#endif

retvalue find_needs_build(struct database *, struct distribution *, architecture_t, component_t onlycomponent, /*@null@*/const char *glob);

#endif
