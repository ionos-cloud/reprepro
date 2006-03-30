/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
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
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "ignore.h"

int ignored[IGN_COUNT];
bool_t ignore[IGN_COUNT];
enum config_option_owner owner_ignore[IGN_COUNT];

void init_ignores(void) {
	int i;
	for( i = 0 ; i < IGN_COUNT ; i++ ) {
		ignored[i] = 0;
		ignore[i] = FALSE;
		owner_ignore[i] = CONFIG_OWNER_DEFAULT;
	}
}

static retvalue set(const char *given,size_t len, bool_t newvalue, enum config_option_owner newowner) {
	int i;
	static const char * const ignores[] = {
#define IGN(what) #what ,
	VALID_IGNORES
#undef IGN
	};

	//TODO: allow multiple values sperated by some sign here...

	for( i = 0 ; i < IGN_COUNT ; i++) {
		if( strncmp(given,ignores[i],len) == 0 && ignores[i][len] == '\0' ) {
			if( owner_ignore[i] <= newowner  ) {
				ignore[i] = newvalue;
				owner_ignore[i] = newowner;
			}
			break;
		}
	}
	if( i == IGN_COUNT ) {
		char *str = strndup(given,len);
		if( IGNORING("Ignoring","To Ignore",ignore,"Unknown --ignore value: '%s'!\n",(str!=NULL)?str:given)) {
			free(str);
			return RET_NOTHING;
		} else {
			free(str);
			return RET_ERROR;
		}
	} else
		return RET_OK;
}

retvalue set_ignore(const char *given,bool_t newvalue, enum config_option_owner newowner) {
	const char *g,*p;
	retvalue r;

	assert( given != NULL);

	g = given;

	while( TRUE ) {
		p = g;
		while( *p != '\0' && *p !=',' )
			p++;
		if( p == g ) {
			fprintf(stderr,"Empty ignore option in --ignore='%s'!\n",given);
			return RET_ERROR_MISSING;
		}
		r = set(g,p-g,newvalue,newowner);
		if( RET_WAS_ERROR(r) )
			return r;
		if( *p == '\0' )
			return RET_OK;
		g = p+1;
	}
}
