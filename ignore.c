/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "ignore.h"

int ignored[IGN_COUNT];
bool_t ignore[IGN_COUNT];

void init_ignores(void) {
	int i;
	for( i = 0 ; i < IGN_COUNT ; i++ ) {
		ignored[i] = 0;
		ignore[i] = FALSE;
	}
}

static retvalue add(const char *given,size_t len) {
	int i;
	static const char * const ignores[] = {
#define IGN(what) #what ,
	VALID_IGNORES
#undef IGN
	};

	//TODO: allow multiple values sperated by some sign here...

	for( i = 0 ; i < IGN_COUNT ; i++) {
		if( strncmp(given,ignores[i],len) == 0 && ignores[i][len] == '\0' ) {
			ignore[i] = TRUE;
			break;
		}
	}
	if( i == IGN_COUNT ) {
		char *str = strndup(given,len);
		if( IGNORING("Ignoring","To Ignore",ignore,"Unknown --ignore value: '%s'!\n",str?str:given)) {
			free(str);
			return RET_NOTHING;
		} else {
			free(str);
			return RET_ERROR;
		}
	} else
		return RET_OK;
}

retvalue add_ignore(const char *given) {
	const char *g,*p;
	retvalue r;

	assert( given != NULL);

	g = given;

	while( 1 ) {
		p = g;
		while( *p != '\0' && *p !=',' )
			p++;
		if( p == g ) {
			fprintf(stderr,"Empty ignore option in --ignore='%s'!\n",given);
			return RET_ERROR_MISSING;
		}
		r = add(g,p-g);
		if( RET_WAS_ERROR(r) )
			return r;
		if( *p == '\0' )
			return RET_OK;
		g = p+1;
	}
}
