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
#include "chunks.h"

extern int verbose;

/* Call action for each chunk in <filename> */
retvalue chunk_foreach(const char *filename,chunkaction action,void *data,int force,int stopwhenok){
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
		if( stopwhenok && RET_IS_OK(ret) )
			break;
	}
	//TODO: check result:
	gzclose(f);
	return result;
}

/* get the next chunk from file f */
char *chunk_read(gzFile f) {
	char *buffer,*bhead;
	size_t m,c,l = 4096;

	m = 4096;
	c = 0;
	bhead = (buffer = (char*)malloc(m));
	if( buffer == NULL )
		return NULL;
	while( gzgets(f,bhead,m-1-c) ) {
		c += (l = strlen(bhead));
		if( *bhead == '\n' ) {
			/* we do not want to include the final newline */
			*bhead = '\0'; c--;
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
static const char *chunk_getfield(const char *name,const char *chunk) {
	size_t l;

	if( !chunk) 
		return NULL;
	l = strlen(name);
	while( *chunk != '\0' ) {
		if(strncmp(name,chunk,l) == 0 && chunk[l] == ':' ) {
			chunk += l+1;
			return chunk;
		}
		while( *chunk != '\n' && *chunk != '\0' )
			chunk++;
		if( *chunk == '\0' || *(++chunk) == '\n' )
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
	while( e > b && *e != '\0' && isspace(*e) )
		e--;
	if( !isspace(*e) )
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
	r = strlist_init(strlist);
	if( RET_WAS_ERROR(r) )
		return r;
	/* walk over the first line */
	while( *f != '\0' && *f != '\n' )
		f++;
	/* nothing there is an emtpy list */
	if( *f == '\0' )
		return RET_OK;
	f++;
	/* while lines begin with ' ', add them */
	while( *f == ' ' ) {
		while( *f != '\0' && isblank(*f) )
			f++;
		b = f;
		while( *f != '\0' && *f != '\n' )
			f++;
		e = f;
		while( e > b && *e != '\0' && isspace(*e) )
			e--;
		if( !isspace(*e) )
			v = strndup(b,e-b+1);
		else 
			v = strdup("");
		if( !v ) {
			strlist_done(strlist);
			return RET_ERROR_OOM;
		}
		r = strlist_add(strlist,v);
		if( !RET_IS_OK(r) ) {
			strlist_done(strlist);
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
	r = strlist_init(strlist);
	if( RET_WAS_ERROR(r) )
		return r;
	while( *f != '\0' ) {
		/* walk over spaces */
		while( *f != '\0' && isspace(*f) ) {
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
		while( *f != '\0' && !isspace(*f) )
			f++;
		v = strndup(b,f-b);
		if( !v ) {
			strlist_done(strlist);
			return RET_ERROR_OOM;
		}
		r = strlist_add(strlist,v);
		if( !RET_IS_OK(r) ) {
			strlist_done(strlist);
			return r;
		}
	}
	return RET_OK;
}

retvalue chunk_gettruth(const char *chunk,const char *name) {
	const char *field;

	field = chunk_getfield(name,chunk);
	if( !field )
		return RET_NOTHING;

	return RET_OK;
}
/* return RET_OK, if field is found, RET_NOTHING, if not */ 
retvalue chunk_checkfield(const char *chunk,const char *name){
	const char *field;

	field = chunk_getfield(name,chunk);
	if( !field )
		return RET_NOTHING;

	return RET_OK;
}

/* Add this the <fields to add> to <chunk> before <beforethis> field,
 * replacing older fields of this name, if they are already there. */

char *chunk_replacefields(const char *chunk,const struct fieldtoadd *toadd,const char *beforethis) {
	const char *c,*ce;
	char *newchunk,*n;
	size_t size,len_beforethis;
	const struct fieldtoadd *f;
	retvalue result;

	assert( chunk != NULL && beforethis != NULL );

	if( toadd == NULL )
		return RET_NOTHING;

	c = chunk;

	/* calculate the maximal size we might end up with */
	size = 1+ strlen(c);
	f = toadd;
	while( f ) {
		size += 3 + f->len_field + f->len_data;
		f = f->next;
	}

	newchunk = n = malloc(size);
	if( n == NULL )
		return NULL;

	len_beforethis = strlen(beforethis);

	result = RET_NOTHING;
	do {
		/* are we at the place to add the fields yet? */
		if(strncmp(c,beforethis,len_beforethis) == 0 
				&& c[len_beforethis] == ':' ) {
			/* add them now: */
			f = toadd;
			while( f ) {
				memcpy(n,f->field,f->len_field);
				n += f->len_field;
				*n = ':'; n++;
				*n = ' '; n++;
				memcpy(n,f->data,f->len_data);
				n += f->len_data;
				*n = '\n'; n++;
				f = f->next;
			}
			result = RET_OK;
		}
		/* is this one of the fields we added/will add? */
		f = toadd;
		while( f ) {
			if(strncmp(c,f->field,f->len_field) == 0 
					&& c[f->len_field] == ':' )
				break;
			f = f->next;
		}
		/* search the end of the field */
		ce = c;
		do {
			while( *ce != '\n' && *ce != '\0' )
				ce++;
			if( *ce == '\0' )
				break;
			ce++;
		} while( *ce == ' ' );

		/* copy it, if it is not to be ignored */

		if( f == NULL ) {
			memcpy(n,c,ce-c);
			n += ce-c;
		}

		/* and proceed with the next */
		c = ce;

	} while( *c != '\0' );

	*n = '\0';

	if( result == RET_NOTHING ) {
		fprintf(stderr,"Could not find field '%s' in chunk '%s'!!!\n",beforethis,chunk);
		assert(0);
	}

	return newchunk;
}

struct fieldtoadd *addfield_new(const char *field,const char *data,struct fieldtoadd *next) {
	struct fieldtoadd *n;

	n = malloc(sizeof(struct fieldtoadd));
	if( n == NULL )
		return n;
	n->field = field;
	n->len_field = strlen(field);
	n->data = data;
	n->len_data = strlen(data);
	n->next = next;
	return n;
}
struct fieldtoadd *addfield_newn(const char *field,const char *data,size_t len,struct fieldtoadd *next) {
	struct fieldtoadd *n;

	n = malloc(sizeof(struct fieldtoadd));
	if( n == NULL )
		return n;
	n->field = field;
	n->len_field = strlen(field);
	n->data = data;
	n->len_data = len;
	n->next = next;
	return n;
}
void addfield_free(struct fieldtoadd *f) {
	struct fieldtoadd *g;

	while( f ) {
		g = f->next;
		free(f);
		f = g;
	}
}

char *chunk_replacefield(const char *chunk,const char *fieldname,const char *data) {
	struct fieldtoadd toadd;

	toadd.field = fieldname;
	toadd.len_field = strlen(fieldname);
	toadd.data = data;
	toadd.len_data = strlen(data);
	toadd.next = NULL;
	return chunk_replacefields(chunk,&toadd,fieldname);
}
