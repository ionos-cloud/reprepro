/*  This file is part of "reprepro"
 *  Copyright (C) 2003 Bernhard R. Link
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

#include <errno.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "error.h"
#include "guesscomponent.h"

/* Guess which component to use:
 * - if the user gave one, use that one.
 * - if the section is a componentname, use this one
 * - if the section starts with a componentname/, use this one
 * - if the section ends with a /componentname, use this one
 * - if the section/ is the start of a componentname, use this one
 * - use the first component in the list
 */

retvalue guess_component(const char *codename, const struct atomlist *components, const char *package, const char *section, component_t givencomponent, component_t *guess) {
	int i;
	size_t section_len;

	if (atom_defined(givencomponent)) {
		if (!atomlist_in(components, givencomponent)) {
			(void)fprintf(stderr,
"Could not find '%s' in components of '%s': ",
					atoms_components[givencomponent],
					codename);
			(void)atomlist_fprint(stderr,
					at_component, components);
			(void)fputs("'\n", stderr);
			return RET_ERROR;
		}
		*guess = givencomponent;
		return RET_OK;
	}
	if (section == NULL) {
		fprintf(stderr,
"Found no section for '%s', so I cannot guess the component to put it in!\n",
				package);
		return RET_ERROR;
	}
	if (components->count <= 0) {
		fprintf(stderr,
"I do not find any components in '%s', so there is no chance I cannot even take one by guessing!\n",
				codename);
		return RET_ERROR;
	}
	section_len = strlen(section);

	for (i = 0 ; i < components->count ; i++) {
		const char *component = atoms_components[components->atoms[i]];

		if (strcmp(section, component) == 0) {
			*guess = components->atoms[i];
			return RET_OK;
		}
	}
	for (i = 0 ; i < components->count ; i++) {
		const char *component = atoms_components[components->atoms[i]];
		size_t len = strlen(component);

		if (len<section_len && section[len] == '/' &&
				strncmp(section, component, len) == 0) {
			*guess = components->atoms[i];
			return RET_OK;
		}
	}
	for (i = 0 ; i < components->count ; i++) {
		const char *component = atoms_components[components->atoms[i]];
		size_t len = strlen(component);

		if (len<section_len && section[section_len-len-1] == '/' &&
				strncmp(section+section_len-len, component, len)
				== 0) {
			*guess = components->atoms[i];
			return RET_OK;
		}
	}
	for (i = 0 ; i < components->count ; i++) {
		const char *component = atoms_components[components->atoms[i]];

		if (strncmp(section, component, section_len) == 0 &&
				component[section_len] == '/') {
			*guess = components->atoms[i];
			return RET_OK;
		}

	}
	*guess = components->atoms[0];
	return RET_OK;
}
