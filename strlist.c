/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2005 Bernhard R. Link
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
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "strlist.h"

bool_t strlist_in(const struct strlist *strlist,const char *element) {
	int c;
	char **t;

	assert(strlist != NULL);

	c = strlist->count; 
	t = strlist->values;
	while( c-- != 0 ) {
		if( strcmp(*(t++),element) == 0 )
			return TRUE;
	}
	return FALSE;
}
int strlist_ofs(const struct strlist *strlist,const char *element) {
	int c;
	char **t;

	assert(strlist != NULL);

	c = strlist->count; 
	t = strlist->values;
	while( c-- != 0 ) {
		if( strcmp(*(t++),element) == 0 )
			return (t-strlist->values)-1;
	}
	return -1;
}

bool_t strlist_subset(const struct strlist *strlist,const struct strlist *subset,const char **missing) {
	int c;
	char **t;

	assert(subset != NULL);

	c = subset->count; 
	t = subset->values;
	while( c-- != 0 ) {
		if( !strlist_in(strlist,*(t++)) ) {
			if( missing != NULL )
				*missing = *(t-1);
			return FALSE;
		}
	}
	return TRUE;

}

retvalue strlist_init_n(int startsize,struct strlist *strlist) {
	assert(strlist != NULL && startsize >= 0);

	if( startsize == 0 )
		startsize = 1;
	strlist->count = 0;
	strlist->size = startsize;
	strlist->values = malloc(startsize*sizeof(char *));
	if( startsize > 0 && strlist->values == NULL )
		return RET_ERROR_OOM;
	return RET_OK;
}

retvalue strlist_init_singleton(char *value,struct strlist *strlist) {
	assert(strlist != NULL);
	
	strlist->count = 1;
	strlist->size = 1;
	strlist->values = malloc(sizeof(char *));
	if( strlist->values == NULL ) {
		free(value);
		return RET_ERROR_OOM;
	}
	strlist->values[0] = value;

	return RET_OK;
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
		if( v == NULL ) {
			free(element);
			return RET_ERROR_OOM;
		}
		strlist->values = v;
	}

	strlist->values[strlist->count++] = element;
	return RET_OK;
}

retvalue strlist_include(struct strlist *strlist, char *element) {
	char **v;

	assert(strlist != NULL && element != NULL);

	if( strlist->count >= strlist->size ) {
		strlist->size += 1;
		v = realloc(strlist->values, strlist->size*sizeof(char *));
		if( v == NULL ) {
			free(element);
			return RET_ERROR_OOM;
		}
		strlist->values = v;
	}
	memmove(strlist->values+1,strlist->values,strlist->count*sizeof(char *));
	strlist->count++;
	strlist->values[0] = element;
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
	if( dest->values == NULL )
		return RET_ERROR_OOM;
	for( i = 0 ; i < dest->count ; i++ ) {
		if( (dest->values[i] = strdup(orig->values[i])) == NULL ) {
			strlist_done(dest);
			return RET_ERROR_OOM;
		}
	}
	return RET_OK;
}

/* replace the contents of dest with those from orig, which get emptied */
void strlist_move(struct strlist *dest,struct strlist *orig) {

	assert(dest != NULL && orig != NULL);

	if( dest == orig )
		return;

	dest->size = orig->size;
	dest->count = orig->count;
	dest->values = orig->values;
	orig->size = orig->count = 0;
	orig->values = NULL;
}
/* empty orig and add everything to the end of dest, in case of error, nothing
 * was done. */
retvalue strlist_mvadd(struct strlist *dest,struct strlist *orig) {
	int i;

	assert(dest != NULL && orig != NULL && dest != orig);

	if( dest->count+orig->count >= dest->size ) {
		int newsize = dest->count+orig->count+8;
		char **v = realloc(dest->values, newsize*sizeof(char *));
		if( v == NULL ) {
			return RET_ERROR_OOM;
		}
		dest->size = newsize;
		dest->values = v;
	}

	for( i = 0 ; i < orig->count ; i++ )
		dest->values[dest->count+i] = orig->values[i];
	dest->count += orig->count;
	free(orig->values);
	orig->size = orig->count = 0;
	orig->values = NULL;

	return RET_OK;
}

retvalue strlist_adduniq(struct strlist *strlist,char *element) {
	// TODO: is there something better feasible?
	if( strlist_in(strlist,element) ) {
		free(element);
		return RET_OK;
	} else
		return strlist_add(strlist,element);
		
}
