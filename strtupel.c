/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2004 Bernhard R. Link
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

#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include "error.h"
#include "strlist.h"
#include "strtupel.h"

const char *strtupel_get(const strtupel *tupel,int index) {
	assert( index >= 0 );

	while( index > 0 ) {
		while( *tupel )
			tupel++;
		tupel++;
		if( *tupel == '\0' ) {
			assert(0);
		}
		index--;
	}
	return tupel;
}

strtupel *strtupel_fromarrays(int count,const char **strings,const size_t *lengths) {
	const char **v;
	const size_t *l;
	int c;
	size_t len;
	strtupel *tupel;
	char *p;

	assert( count > 0 && strings != NULL && lengths != NULL );

	len = 1; l =lengths ; c = count;
	while( c ) {
		len += *l + 1;
		l++;c--;
	}
	tupel = malloc(len);
	if( tupel == NULL ) {
		return NULL;
	}
	p = tupel ; v = strings; l = lengths ; c = count;
	while( c ) {
		assert( *l > 0 );
		memcpy(p,*v,*l+1);
		p += *l + 1;
		v++;l++;c--;
	}
	*p = '\0';
	return tupel;
}

strtupel *strtupel_fromvalues(int count,...) {
	va_list ap;
	char **strings,**v;
	size_t *lengths,*l;
	int c;
	strtupel *tupel;

	assert( count > 0 );

	strings = malloc(count*sizeof(char*));
	lengths = malloc(count*sizeof(size_t));
	if( strings == NULL || lengths == NULL ) {
		free(strings);
		return NULL;
	}

	va_start(ap,count);
		 
	c = count; v = strings; l = lengths;
	while( c ) {
		*v = va_arg(ap,char*);
		*l = strlen(*v);
		v++;l++;c--;
	}

	tupel = strtupel_fromarrays(count,(const char **)strings,lengths);

	va_end(ap);

	free(strings);free(lengths);

	return tupel;
}

size_t strtupel_len(const strtupel *tupel) {
	const char *p;

	p = tupel;
	do {
		while( *p ) {
			p++;
		}
		p++;
	} while( *p );

	return (p-tupel);
}

const strtupel *strtupel_next(const strtupel *tupel) {
	const char *p;

	p = tupel;
	if( *p == '\0' )
		return tupel;
	while( *p ) {
		p++;
	}
	p++;
	return p;
}

int strtupel_empty(const strtupel *tupel) {

	return (*tupel == '\0');
}

retvalue strtupel_print(FILE *file,const strtupel *tupel) {
	const strtupel *p;

	p = tupel;

	while( !strtupel_empty(p) ){
		fprintf(file,"'%s' ",p);
		p = strtupel_next(p);
	}
	return RET_OK;
}
