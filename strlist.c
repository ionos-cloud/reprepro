/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2003 Bernhard R. Link
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
#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "strlist.h"

int strlist_in(const struct strlist *strlist,const char *element) {
	int c;
	char **t;

	assert(strlist != NULL);

	c = strlist->count; 
	t = strlist->values;
	while( c-- != 0 ) {
		if( strcmp(*(t++),element) == 0 )
			return 1;
	}
	return 0;
}

retvalue strlist_init(struct strlist *strlist) {
	assert(strlist != NULL);
	
	strlist->count = 0;
	strlist->size = 0;
	strlist->values = NULL;

	return RET_OK;
}

void strlist_done(struct strlist *strlist) {
	int c;
	char **t;

	assert(strlist != NULL);

	c = strlist->count; 
	t = strlist->values;
	while( c-- != 0 ) {
		free(*t);
		t++;
	}
	free(strlist->values);
	strlist->values = NULL;
}

retvalue strlist_add(struct strlist *strlist, char *element) {
	char **v;

	assert(strlist != NULL && element != NULL);

	if( strlist->count >= strlist->size ) {
		strlist->size += 8;
		v = realloc(strlist->values, strlist->size*sizeof(char *));
		if( !v ) {
			free(element);
			return RET_ERROR_OOM;
		}
		strlist->values = v;
	}

	strlist->values[strlist->count++] = element;
	return RET_OK;
}

retvalue strlist_fprint(FILE *file,const struct strlist *strlist) {
	int c;
	char **p;
	retvalue result;

	assert(strlist != NULL);
	assert(file != NULL);

	c = strlist->count;
	p = strlist->values;
	result = RET_OK;
	while( c > 0 ) {
		if( fputs(*(p++),file) == EOF )
			result = RET_ERROR;
		if( --c > 0 && fputc(' ',file) == EOF )
			result = RET_ERROR;
	}
	return result;
}

/* duplicate with content */
retvalue strlist_dup(struct strlist *dest,const struct strlist *orig) {
	int i;

	assert(dest != NULL && orig != NULL);
	
	dest->size = dest->count = orig->count;
	dest->values = calloc(dest->count,sizeof(char*));;
	if( !dest->values )
		return RET_ERROR_OOM;
	for( i = 0 ; i < dest->count ; i++ ) {
		if( !(dest->values[i] = strdup(orig->values[i])) ) {
			strlist_done(dest);
			return RET_ERROR_OOM;
		}
	}
	return RET_OK;
}
