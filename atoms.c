/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2008,2009 Bernhard R. Link
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
#include <stdlib.h>
#include <stdio.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "atoms.h"

const char **atoms_architectures;
const char **atoms_components;
const char * const packagetypes[5] = { "!!NONE!!", "dsc", "deb", "udeb", "ddeb" };
const char **atoms_packagetypes = (const char **)&packagetypes;
const char **atoms_commands;
static int command_count;
static const char * const types[4] = {
	"architecture", "component", "packagetype", "command"
};
const char **atomtypes = (const char **)types;

/* trivial implementation for now, perhaps make it more complicated later */
static struct strlist architectures, components;

retvalue atoms_init(int count) {
	retvalue r;
	strlist_init(&architectures);
	strlist_init(&components);

	/* add a 0th entry to all, so 0 means uninitialized */

	r = strlist_add_dup(&architectures, "!!NONE!!");
	if (RET_WAS_ERROR(r))
		return r;
	r = strlist_add_dup(&architectures, "source");
	if (RET_WAS_ERROR(r))
		return r;
	r = strlist_add_dup(&architectures, "all");
	if (RET_WAS_ERROR(r))
		return r;
	r = strlist_add_dup(&components, "!!NONE!!");
	if (RET_WAS_ERROR(r))
		return r;
	/* a fallback component to put things without a component in */
	r = strlist_add_dup(&components, "strange");
	if (RET_WAS_ERROR(r))
		return r;
	atoms_components = (const char**)components.values;
	atoms_architectures = (const char**)architectures.values;
	command_count = count;
	if (command_count > 0) {
		atoms_commands = nzNEW(command_count + 1, const char*);
		if (FAILEDTOALLOC(atoms_commands))
			return RET_ERROR_OOM;
	}
	return RET_OK;
}

retvalue architecture_intern(const char *value, architecture_t *atom_p) {
	retvalue r;
	int i;

	i = strlist_ofs(&architectures, value);
	if (i >= 0) {
		*atom_p = (architecture_t)i;
		return RET_OK;
	}
	i = architectures.count;
	r = strlist_add_dup(&architectures, value);
	atoms_architectures = (const char**)architectures.values;
	if (RET_IS_OK(r)) {
		*atom_p = (architecture_t)i;
		return RET_OK;
	} else
		return r;
}
retvalue component_intern(const char *value, component_t *atom_p) {
	retvalue r;
	int i;

	i = strlist_ofs(&components, value);
	if (i >= 0) {
		*atom_p = (component_t)i;
		return RET_OK;
	}
	i = components.count;
	r = strlist_add_dup(&components, value);
	atoms_components = (const char**)components.values;
	if (RET_IS_OK(r)) {
		*atom_p = (component_t)i;
		return RET_OK;
	} else
		return r;
}

architecture_t architecture_find(const char *value) {
	int i = strlist_ofs(&architectures, value);
	if (i < 0)
		return atom_unknown;
	else
		return (architecture_t)i;
}

architecture_t architecture_find_l(const char *value, size_t l) {
	architecture_t a;

	for (a = architectures.count - 1 ; a > 0 ; a--) {
		const char *name = atoms_architectures[a];
		size_t len = strlen(name);

		if (len == l && memcmp(name, value, len) == 0)
			return a;
	}
	return atom_unknown;
}

// TODO: this might be called a lot, perhaps optimize it...
component_t component_find_l(const char *value, size_t l) {
	component_t a;

	for (a = components.count - 1 ; a > 0 ; a--) {
		const char *name = atoms_components[a];
		size_t len = strlen(name);

		if (len == l && memcmp(name, value, len) == 0)
			return a;
	}
	return atom_unknown;
}

component_t component_find(const char *value) {
	int i = strlist_ofs(&components, value);
	if (i < 0)
		return atom_unknown;
	else
		return (architecture_t)i;
}

packagetype_t packagetype_find(const char *value) {
	if (strcmp(value, "dsc") == 0)
		return pt_dsc;
	else if (strcmp(value, "deb") == 0)
		return pt_deb;
	else if (strcmp(value, "udeb") == 0)
		return pt_udeb;
	else if (strcmp(value, "ddeb") == 0)
		return pt_ddeb;
	else
		return atom_unknown;
}

packagetype_t packagetype_find_l(const char *value, size_t len) {
	if (len == 3) {
		if (strncmp(value, "dsc", 3) == 0)
			return pt_dsc;
		else if (strncmp(value, "deb", 3) == 0)
			return pt_deb;
	} else if (len == 4) {
		if (strncmp(value, "udeb", 4) == 0)
			return pt_udeb;
		else if (strncmp(value, "ddeb", 4) == 0)
			return pt_ddeb;
	}
	return atom_unknown;
}

static inline command_t command_find(const char *value) {
	command_t c;

	for (c = command_count ; c > 0 ; c--) {
		if (strcmp(atoms_commands[c], value) == 0)
			return c;
	}
	return atom_unknown;
}

atom_t atom_find(enum atom_type type, const char *value) {
	switch (type) {
		case at_packagetype:
			return packagetype_find(value);
		case at_architecture:
			return architecture_find(value);
		case at_component:
			return component_find(value);
		case at_command:
			return command_find(value);
		default:
			return atom_unknown;
	}
}

retvalue atom_intern(enum atom_type type, const char *value, atom_t *atom_p) {
	assert (type == at_architecture || type == at_component);
	switch (type) {
		case at_architecture:
			return architecture_intern(value, atom_p);
		case at_component:
			return component_intern(value, atom_p);
		default:
			return RET_ERROR;
	}
}

void atomlist_init(struct atomlist *list) {
	list->count = 0; list->size = 0;
	list->atoms = 0;
}

void atomlist_done(struct atomlist *list) {
	if (list->size > 0) {
		assert (list->atoms != 0);
		free(list->atoms);
	}
	/* reset atoms but not size, so reuse can be caught */
	list->atoms = NULL;
}

/* add a atom uniquely (perhaps sorted), RET_NOTHING when already there */
retvalue atomlist_add_uniq(struct atomlist *list, atom_t atom) {
	int i;
	atom_t *n;

	assert (atom_defined(atom));

	for (i = 0 ; i < list->count ; i++) {
		if (list->atoms[i] == atom)
			return RET_NOTHING;
	}
	if (list->size <= list->count) {
		n = realloc(list->atoms, (sizeof(atom_t))*(list->count + 8));
		if (FAILEDTOALLOC(n))
			return RET_ERROR_OOM;
		list->size = list->count + 8;
		list->atoms = n;
	}
	list->atoms[list->count++] = atom;
	return RET_OK;
}

retvalue atomlist_add(struct atomlist *list, atom_t atom) {
	atom_t *n;

	assert (atom_defined(atom));

	if (list->size <= list->count) {
		n = realloc(list->atoms, (sizeof(atom_t))*(list->count + 8));
		if (FAILEDTOALLOC(n))
			return RET_ERROR_OOM;
		list->size = list->count + 8;
		list->atoms = n;
	}
	list->atoms[list->count++] = atom;
	return RET_OK;
}

/* replace the contents of dest with those from orig, which get emptied */
void atomlist_move(struct atomlist *dest, struct atomlist *orig) {
	dest->atoms = orig->atoms;
	dest->count = orig->count;
	dest->size = orig->size;
	/* reset atoms but not size, so reuse can be caught */
	orig->atoms = NULL;
}

bool atomlist_hasexcept(const struct atomlist *list, atom_t atom) {
	int i;

	for (i = 0 ; i < list->count ; i++) {
		if (list->atoms[i] != atom)
			return true;
	}
	return false;
}

bool atomlist_in(const struct atomlist *list, atom_t atom) {
	int i;

	for (i = 0 ; i < list->count ; i++) {
		if (list->atoms[i] == atom)
			return true;
	}
	return false;
}
int atomlist_ofs(const struct atomlist *list, atom_t atom) {
	int i;

	for (i = 0 ; i < list->count ; i++) {
		if (list->atoms[i] == atom)
			return i;
	}
	return -1;
}

bool atomlist_subset(const struct atomlist *list, const struct atomlist *subset, atom_t *missing) {
	int i, j;

	for (j = 0 ; j < subset->count ; j++) {
		atom_t atom = subset->atoms[j];

		for (i = 0 ; i < list->count ; i++) {
			if (list->atoms[i] == atom)
				break;
		}
		if (i >= list->count) {
			if (missing != NULL)
				*missing = atom;
			return false;
		}
	}
	return true;
}

retvalue atomlist_fprint(FILE *file, enum atom_type type, const struct atomlist *list) {
	const char **atoms = NULL;
	int c;
	atom_t *p;
	retvalue result;

	assert(list != NULL);
	assert(file != NULL);

	switch (type) {
		case at_architecture:
			atoms = atoms_architectures;
			break;
		case at_component:
			atoms = atoms_components;
			break;
		case at_packagetype:
			atoms = atoms_packagetypes;
			break;
		case at_command:
			atoms = atoms_commands;
			break;
	}
	assert(atoms != NULL);

	c = list->count;
	p = list->atoms;
	result = RET_OK;
	while (c > 0) {
		if (fputs(atoms[*(p++)], file) == EOF)
			result = RET_ERROR;
		if (--c > 0 && fputc(' ', file) == EOF)
			result = RET_ERROR;
	}
	return result;
}

component_t components_count(void) {
	return components.count;
}

retvalue atomlist_filllist(enum atom_type type, struct atomlist *list, char *string, const char **missing) {
	struct atomlist l;
	char *e;
	retvalue r;
	atom_t a;

	atomlist_init(&l);
	while (*string != '\0') {
		e = strchr(string, '|');
		if (e == NULL)
			e = strchr(string, '\0');
		else
			*(e++) = '\0';
		a = atom_find(type, string);
		if (!atom_defined(a)) {
			atomlist_done(&l);
			*missing = string;
			return RET_NOTHING;
		}
		r = atomlist_add(&l, a);
		if (RET_WAS_ERROR(r)) {
			atomlist_done(&l);
			return r;
		}
		string = e;
	}
	atomlist_move(list, &l);
	return RET_OK;
}
