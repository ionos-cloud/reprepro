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
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <zlib.h>
#include <assert.h>
#include "error.h"
#include "mprintf.h"
#include "chunks.h"

extern int verbose;

/* Call action for each chunk in <filename> */
retvalue chunk_foreach(const char *filename,chunkaction action,void *data,int force){
	gzFile f;
	retvalue result,ret;
	char *chunk;

	f = gzopen(filename,"r");
	if( !f ) {
		fprintf(stderr,"Unable to open file %s: %m\n",filename);
		return RET_ERRNO(errno);
	}
	result = RET_NOTHING;
	while( (chunk = chunk_read(f))) {
		ret = action(data,chunk);

		RET_UPDATE(result,ret);

		free(chunk);

		if( RET_WAS_ERROR(ret) && !force ) {
			if( verbose > 0 )
				fprintf(stderr,"Stop reading further chunks from '%s' due to privious errors.\n",filename);
			break;
		}
	}
	gzclose(f);
	return result;
}

/* get the next chunk from file f */
char *chunk_read(gzFile f) {
	char *buffer,*bhead;
	int m,c,l;

	m = 4096;
	c = 0;
	bhead = (buffer = (char*)malloc(m));
	while( gzgets(f,bhead,m-1-c) ) {
		c += (l = strlen(bhead));
		if( *bhead == '\n' ) {
			/* we do not want to include the final newline */
			*bhead = 0; c--;
			if( c == 0 ) {
				bhead = buffer;
				continue;
			}
			buffer = realloc(buffer,c+1);
			return buffer;
		}
		while( bhead[l-1] != '\n' ) {
			if( m-c < 100 ) {
				m *= 2;
				buffer = realloc(buffer,m);
				if( !buffer )
					return NULL;
				
			}
			bhead = buffer + c;
			if( !gzgets(f,bhead,m-1-c)) {
				buffer = realloc(buffer,c+1);
				return buffer;
			}
			c += (l = strlen(bhead));
		}
		if( m-c < 100 ) {
			m *= 2;
			buffer = realloc(buffer,m);
			if( !buffer )
				return NULL;

		}
		bhead = buffer + c;
	}
	if( c == 0 ) {
		free(buffer);
		return NULL;
	} else {
		buffer = realloc(buffer,c+1);
		return buffer;
	}
}

/* point to a specified field in a chunk */
const char *chunk_getfield(const char *name,const char *chunk) {
	int l;

	if( !chunk) 
		return NULL;
	l = strlen(name);
	while( *chunk ) {
		if(strncmp(name,chunk,l) == 0 && chunk[l] == ':' ) {
			chunk += l+1;
			return chunk;
		}
		while( *chunk != '\n' && *chunk != '\0' )
			chunk++;
		if( ! *chunk || *(++chunk) == '\n' )
			return NULL;
	}
	return NULL;
}





/*
char *chunk_dupnextline(const char **field) {
	const char *h,*r;

	if( !field)
		return NULL;
	h = *field;
	if( isspace(*h) )
		h++;
	r = h;
	while( *h != '\n' && *h != '\0' )
		h++;
	*field = h;
	if( **field == '\n' )
		(*field)++;
	if( !isspace( **field ) )
		*field = NULL;
	return strndup(r,h-r);
}
*/

/* create a new chunk with the context of field name replaced with new */
char *chunk_replaceentry(const char *chunk,const char *name,const char *new) {
	int l;
	const char *olddata,*rest;
	char *result;

	if( !chunk || !name || !new ) 
		return NULL;
	/* first search for the old field */
	l = strlen(name);
	olddata = chunk;
	do {
		if(strncmp(name,olddata,l) == 0 && olddata[l] == ':' ) {
			olddata += l+1;
			/* now search the end of the data to be replaced */
			rest = olddata;
			while( *rest && *rest != '\n' )
				rest++;
			/* create a chunk with the appopiate line replaced */
			result = malloc(2+strlen(rest)+strlen(new)+(olddata-chunk));
			if( !result )
				return NULL;
			strncpy(result,chunk,olddata-chunk);
			result[olddata-chunk] = ' ';
			strcpy(result+(olddata-chunk)+1,new);
			strcat(result,rest);
			return result;
		}
		while( *olddata != '\n' && *olddata != '\0' )
			olddata++;
		if( *olddata == '\0' )
			break;
		olddata++;
		/* Reading a chunk should have ended there: */
		assert(*olddata != '\n');
	} while( *olddata );
	fprintf(stderr,"not finding '%s', so appending setting to '%s'\n",name,new);
	/* not found, so append */
	return mprintf("%s\n%s: %s",chunk,name,new);
}

/* look for name in chunk. returns RET_NOTHING if not found */
retvalue chunk_getvalue(const char *chunk,const char *name,char **value) {
	const char *field;
	char *val;
	const char *b,*e;

	assert(value != NULL);
	field = chunk_getfield(name,chunk);
	if( !field )
		return RET_NOTHING;

	b = field;
	/* jump over spaces at the beginning */
	if( isspace(*b) )
		b++;
	/* search for the end */
	e = b;
	while( *e != '\n' && *e != '\0' )
		e++;
	/* remove trailing spaced */
	while( e > b && *e && isspace(*e) )
		e--;
	if( e != b )
		val = strndup(b,e-b+1);
	else 
		val = strdup("");
	if( !val )
		return RET_ERROR_OOM;
	*value = val;
	return RET_OK;
}

retvalue chunk_getfirstword(const char *chunk,const char *name,char **value) {
	const char *field;
	char *val;
	const char *b,*e;

	assert(value != NULL);
	field = chunk_getfield(name,chunk);
	if( !field )
		return RET_NOTHING;

	b = field;
	if( isspace(*b) )
		b++;
	e = b;
	while( !isspace(*e) && *e != '\n' && *e != '\0' )
		e++;
	val = strndup(b,e-b);
	if( !val )
		return RET_ERROR_OOM;
	*value = val;
	return RET_OK;
}

retvalue chunk_getextralinelist(const char *chunk,const char *name,struct strlist *strlist) {
	retvalue r;
	const char *f,*b,*e;
	char *v;

	f = chunk_getfield(name,chunk);
	if( !f )
		return RET_NOTHING;
	r = strlist_new(strlist);
	if( RET_WAS_ERROR(r) )
		return r;
	/* walk over the first line */
	while( *f && *f != '\n' )
		f++;
	/* nothing there is an emtpy list */
	if( *f == '\0' )
		return RET_OK;
	f++;
	/* while lines begin with ' ', add them */
	while( *f == ' ' ) {
		while( *f && isblank(*f) )
			f++;
		b = f;
		while( *f && *f != '\n' )
			f++;
		e = f;
		while( e > b && *e && isspace(*e) )
			e--;
		if( e != b )
			v = strndup(b,e-b+1);
		else 
			v = strdup("");
		if( !v ) {
			strlist_free(strlist);
			return RET_ERROR_OOM;
		}
		r = strlist_add(strlist,v);
		if( !RET_IS_OK(r) ) {
			strlist_free(strlist);
			return r;
		}
		if( *f == '\0' )
			return RET_OK;
		f++;
	}
	return RET_OK;
}

retvalue chunk_getwordlist(const char *chunk,const char *name,struct strlist *strlist) {
	retvalue r;
	const char *f,*b;
	char *v;

	f = chunk_getfield(name,chunk);
	if( !f )
		return RET_NOTHING;
	r = strlist_new(strlist);
	if( RET_WAS_ERROR(r) )
		return r;
	while( *f != '\0' ) {
		/* walk over spaces */
		while( *f && isspace(*f) ) {
			if( *f == '\n' ) {
				f++;
				if( *f != ' ' )
					return RET_OK;
			} else
				f++;
		}
		if( *f == '\0' )
			return RET_OK;
		b = f;
		/* search for end of word */
		while( *f && !isspace(*f) )
			f++;
		v = strndup(b,f-b);
		if( !v ) {
			strlist_free(strlist);
			return RET_ERROR_OOM;
		}
		r = strlist_add(strlist,v);
		if( !RET_IS_OK(r) ) {
			strlist_free(strlist);
			return r;
		}
	}
	return RET_OK;
}
