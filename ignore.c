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

#include <stdio.h>
#include <string.h>

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

retvalue add_ignore(const char *given) {
	int i;
	static const char *ignores[] = {
#define IGN(what) #what ,
	VALID_IGNORES
#undef IGN
	};

	//TODO: allow multiple values sperated by some sign here...

	for( i = 0 ; i < IGN_COUNT ; i++) {
		if( strcmp(given,ignores[i]) == 0 ) {
			ignore[i] = TRUE;
			break;
		}
	}
	if( i == IGN_COUNT ) {
		if( IGNORING("Ignoring","To Ignore",ignore,"Unknown --ignore value: '%s'!\n",given))
			return RET_NOTHING;
		else
			return RET_ERROR;
	} else
		return RET_OK;
}
