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
#include "strlist.h"
#include "guesscomponent.h"

extern int verbose;

/* Guess which component to use:
 * - if the user gave one, use that one.
 * - if the section is a componentname, use this one
 * - if the section starts with a componentname/, use this one
 * - if the section ends with a /componentname, use this one
 * - if the section/ is the start of a componentname, use this one
 * - use the first component in the list
 */

retvalue guess_component(const char *codename,const struct strlist *components,
			const char *package,const char *section,
			const char *givencomponent,char **guess) {
	int i;
	size_t section_len;

#define RETURNTHIS(comp) { \
		char *c = strdup(comp); \
		if( c == NULL ) \
			return RET_ERROR_OOM; \
		*guess = c; \
		return RET_OK; \
	}

	if( givencomponent != NULL ) {
		if( !strlist_in(components,givencomponent) ) {
			(void)fprintf(stderr,"Could not find '%s' in components of '%s': ",
					givencomponent,codename);
			(void)strlist_fprint(stderr,components);
			(void)fputs("'\n",stderr);
			return RET_ERROR;
		}

		RETURNTHIS(givencomponent);
	}
	if( section == NULL ) {
		fprintf(stderr,"Found no section for '%s', so I cannot guess the component to put it in!\n",package);
		return RET_ERROR;
	}
	if( components->count <= 0 ) {
		fprintf(stderr,"I do not find any components in '%s', so there is no chance I cannot even take one by guessing!\n",codename);
		return RET_ERROR;
	}
	section_len = strlen(section);

	for( i = 0 ; i < components->count ; i++ ) {
		const char *component = components->values[i];

		if( strcmp(section,component) == 0 )
			RETURNTHIS(component);
	}
	for( i = 0 ; i < components->count ; i++ ) {
		const char *component = components->values[i];
		size_t len = strlen(component);

		if( len<section_len && section[len] == '/' &&
				strncmp(section,component,len) == 0 )
			RETURNTHIS(component);
	}
	for( i = 0 ; i < components->count ; i++ ) {
		const char *component = components->values[i];
		size_t len = strlen(component);

		if( len<section_len && section[section_len-len-1] == '/' &&
				strncmp(section+section_len-len,component,len) == 0 )
			RETURNTHIS(component);
	}
	for( i = 0 ; i < components->count ; i++ ) {
		const char *component = components->values[i];

		if( strncmp(section,component,section_len) == 0 && component[section_len] == '/' )
			RETURNTHIS(component);
	}
	RETURNTHIS(components->values[0]);
#undef RETURNTHIS
}
