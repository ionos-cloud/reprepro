/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2008 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include <config.h>

#include <assert.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "atoms.h"

const char **atoms_architectures;
const char **atoms_components;
const char * const packagetypes[3] = { "dsc", "deb", "udeb" };
const char **atoms_packagetype = (const char **)&packagetypes;

/* trivial implementation for now, perhaps make it more complicated later */
static struct strlist architectures, components;

retvalue atoms_init(void) {
	retvalue r;
	strlist_init(&architectures);
	strlist_init(&components);

	r = strlist_add_dup(&architectures, "all");
	if( RET_WAS_ERROR(r) )
		return r;
	atoms_components = (const char**)components.values;
	atoms_architectures = (const char**)architectures.values;
	return RET_OK;
}

retvalue architecture_intern(const char *value, architecture_t *atom_p) {
	retvalue r;
	int i;

	i = strlist_ofs(&architectures, value);
	if( i >= 0 ) {
		*atom_p = (architecture_t)i;
		return RET_OK;
	}
	i = architectures.count;
	r = strlist_add_dup(&architectures, value);
	atoms_architectures = (const char**)architectures.values;
	if( RET_IS_OK(r) ) {
		*atom_p = (architecture_t)i;
		return RET_OK;
	} else
		return r;
}
retvalue component_intern(const char *value, component_t *atom_p) {
	retvalue r;
	int i;

	i = strlist_ofs(&components, value);
	if( i >= 0 ) {
		*atom_p = (component_t)i;
		return RET_OK;
	}
	i = components.count;
	r = strlist_add_dup(&components, value);
	atoms_components = (const char**)components.values;
	if( RET_IS_OK(r) ) {
		*atom_p = (component_t)i;
		return RET_OK;
	} else
		return r;
}

architecture_t architecture_find(const char *value) {
	return (architecture_t)strlist_ofs(&architectures, value);
}
component_t component_find(const char *value) {
	return (component_t)strlist_ofs(&components, value);
}
packagetype_t packagetype_find(const char *value) {
	if( strcmp(value, "dsc") == 0 )
		return (packagetype_t)0;
	else if( strcmp(value, "deb") == 0 )
		return (packagetype_t)1;
	else if( strcmp(value, "udeb") == 0 )
		return (packagetype_t)2;
	else
		return atom_unknown;
}
