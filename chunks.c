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

/* strdup a field given by chunk_getfield */
char *chunk_dupvalue(const char *field) {
	const char *h;

	if( !field)
		return NULL;
	if( isspace(*field) )
		field++;
	h = field;
	while( *h != '\n' && *h != '\0' )
		h++;
	return strndup(field,h-field);
}

/* strdup without a leading "<prefix>/" 
char *chunk_dupvaluecut(const char *field,const char *prefix) {
	const char *h;
	int l;

	if( !field)
		return NULL;
	if( isspace(*field) )
		field++;
	l = strlen(prefix);
	if( strncmp(field,prefix,l) == 0 && field[l] == '/' ) {
		field += l+1;		
	} else 
		return NULL;
	h = field;
	while( *h != '\n' && *h != '\0' )
		h++;
	return strndup(field,h-field);
} */

/* strdup the following lines of a field */
char *chunk_dupextralines(const char *field) {
	const char *h;

	if( !field)
		return NULL;
	if( *field && *field != '\n' )
		field++;
	h = field;
	if( *h == '\0' )
		return NULL;
	h++;
	while( *h == ' ' ) {
		while( *h != '\n' && *h != '\0' )
			h++;
		if( *h )
			h++;
	}
	return strndup(field,h-field);
}


/* strdup the first word of a field */
char *chunk_dupword(const char *field) {
	const char *h;

	if( !field)
		return NULL;
	if( isspace(*field) )
		field++;
	h = field;
	while( !isspace(*h) && *h != '\n' && *h != '\0' )
		h++;
	return strndup(field,h-field);
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
	//TODO: implement this natively instead of _dupvalue and get rid of the old...
	const char *field;
	char *val;

	assert(*value != NULL);
	field = chunk_getfield(name,chunk);
	if( !field )
		return RET_NOTHING;
	val = chunk_dupvalue(field);
	if( !val )
		return RET_ERROR_OOM;
	*value = val;
	return RET_OK;
}


retvalue chunk_worditerator_get(const struct worditerator *iterator,char **word) {
	const char *p;
	char *w;

	p = iterator->c;
	if( !*p || *p == '\n' )
		return RET_NOTHING;
	while( *p && !isspace(*p) ) {
		p++;
	}
	w = strndup(iterator->c,p-iterator->c);
	if( !w )
		return RET_ERROR_OOM;
	*word = w;
	return RET_OK;
}

retvalue chunk_worditerator_next(struct worditerator *iterator) {
	const char *c;
	
	c = iterator->c;
	while( *c && !isspace(*c) )
		c++;
	while( *c && *c != '\n' && isspace(*c) )
		c++;
	iterator->c = c;
	return RET_OK;
}

/* get a word iterator for the given field */
retvalue chunk_getworditerator(const char *chunk,const char *name,struct worditerator *iterator) {
	const char *field;

	assert(iterator != NULL);
	field = chunk_getfield(name,chunk);
	if( !field )
		return RET_NOTHING;
	while( *field && isspace(*field) )
		field++;
	iterator->c = field;
	return RET_OK;
}
