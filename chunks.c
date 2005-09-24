/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
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
#include "ignore.h"
#include "chunks.h"
#include "names.h"

extern int verbose;

//TODO: this should now also be able to parse \r\n terminated lines instead
// of only \n terminated oned. Though this has still to be tested properly...

/* Call action for each chunk in <filename> */
retvalue chunk_foreach(const char *filename,chunkaction action,void *data,int force,bool_t stopwhenok){
	gzFile f;
	retvalue result,ret;
	char *chunk;

	f = gzopen(filename,"r");
	if( f == NULL ) {
		fprintf(stderr,"Unable to open file %s: %m\n",filename);
		return RET_ERRNO(errno);
	}
	result = RET_NOTHING;
	while( (ret = chunk_read(f,&chunk)) == RET_OK ) {
		ret = action(data,chunk);

		// in this one a segfault was reported, where did
		// it come from?
		free(chunk);

		if( RET_WAS_ERROR(ret) && force <= 0 ) {
			if( verbose > 0 )
				fprintf(stderr,"Stop reading further chunks from '%s' due to privious errors.\n",filename);
			break;
		}
		if( stopwhenok && RET_IS_OK(ret) )
			break;
		RET_UPDATE(result,ret);
	}
	RET_UPDATE(result,ret);
	//TODO: check result:
	gzclose(f);
	return result;
}

/* get the next chunk from file f ( return RET_NOTHING, if there are none )*/
retvalue chunk_read(gzFile f,char **chunk) {
	char *buffer,*bhead;
	size_t size,already,without,l;
	bool_t afternewline = FALSE;

	size = 4096;
	already = 0; without = 0;
	bhead = buffer = (char*)malloc(size);
	if( buffer == NULL )
		return RET_ERROR_OOM;
	while( gzgets(f,bhead,size-1-already) != NULL ) {
		char *p;
		p = bhead;
		while( *p != '\0' ) {
			if( *p != '\r' && *p != '\n' )
				without = 1 + p - buffer;
			p++;
		}
		if( without == 0 ) {
			/* ignore leading newlines... */
			bhead = buffer;
			already = 0;
			continue;
		}
		l = strlen(bhead);
		/* if we are after a newline, and have a new newline,
		 * and only '\r' in between, then return the chunk: */
		if( afternewline && without < already && l!=0 && bhead[l-1] == '\n' ) {
			break;
		}
		already += l;
		if( l != 0 ) // a bit of parania...
			afternewline = bhead[l-1] == '\n';
		if( size-already < 1024 ) {
			char *n;
			if( size >= 513*1024*1024 ) {
				fprintf(stderr,"Will not process chunks larger than half a gigabyte!\n");
				free(buffer);
				return RET_ERROR;
			}
			size *= 2;
			n = realloc(buffer,size);
			if( n == NULL ) {
				free(buffer);
				return RET_ERROR_OOM;
			}
			buffer = n;
		}
		bhead = buffer + already;
	}
	if( without == 0 ) {
		free(buffer);
		return RET_NOTHING;
	} else {
		char *n;
		/* we do not want to include the final newlines */
		buffer[without] = '\0';
		n = realloc(buffer,without+1);
		if( n == NULL ) {
			/* guess this will not happen, but... */
			free(buffer);
			return RET_ERROR_OOM;
		}
		buffer = n;
		*chunk = buffer;
		return RET_OK;
	}
}

/* point to a specified field in a chunk */
static const char *chunk_getfield(const char *name,const char *chunk) {
	size_t l;

	if( chunk == NULL ) 
		return NULL;
	l = strlen(name);
	while( *chunk != '\0' ) {
		if(strncasecmp(name,chunk,l) == 0 && chunk[l] == ':' ) {
			chunk += l+1;
			return chunk;
		}
		while( *chunk != '\n' && *chunk != '\0' )
			chunk++;
		if( *chunk == '\0' )
			return NULL;
		chunk++;
	}
	return NULL;
}

/* get the content of the given field, including all following lines, in a format
 * that may be put into chunk_replacefields */
retvalue chunk_getcontent(const char *chunk,const char *name,char **value) {
	const char *field;
	char *val;
	const char *b,*e;

	assert(value != NULL);
	field = chunk_getfield(name,chunk);
	if( field == NULL )
		return RET_NOTHING;

	b = field;
	/* jump over spaces at the beginning */
	if( xisspace(*b) )
		b++;

	/* search for the end */
	e = b;
	do {
		while( *e != '\n' && *e != '\0' )
			e++;
		if( *e != '\0' )
			e++;
	} while( *e != ' ' && *e != '\t' && *e != '\0' );

	if( e > b && *e == '\0' )
		e--;
	/* remove trailing newline */
	if( e > b && *e == '\n' )
		e--;
	if( e > b )
		val = strndup(b,e-b+1);
	else 
		val = strdup("");
	if( val == NULL )
		return RET_ERROR_OOM;
	*value = val;
	return RET_OK;
}

/* look for name in chunk. returns RET_NOTHING if not found */
retvalue chunk_getvalue(const char *chunk,const char *name,char **value) {
	const char *field;
	char *val;
	const char *b,*e;

	assert(value != NULL);
	field = chunk_getfield(name,chunk);
	if( field == NULL )
		return RET_NOTHING;

	b = field;
	/* jump over spaces at the beginning */
	if( xisspace(*b) )
		b++;
	/* search for the end */
	e = b;
	while( *e != '\n' && *e != '\0' )
		e++;
	/* remove trailing spaces */
	while( e > b && xisspace(*e) )
		e--;
	if( !xisspace(*e) )
		val = strndup(b,e-b+1);
	else 
		val = strdup("");
	if( val == NULL )
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
	if( field == NULL )
		return RET_NOTHING;

	b = field;
	if( xisspace(*b) )
		b++;
	e = b;
	while( !xisspace(*e) && *e != '\n' && *e != '\0' )
		e++;
	val = strndup(b,e-b);
	if( val == NULL )
		return RET_ERROR_OOM;
	*value = val;
	return RET_OK;
}

retvalue chunk_getextralinelist(const char *chunk,const char *name,struct strlist *strlist) {
	retvalue r;
	const char *f,*b,*e;
	char *v;

	f = chunk_getfield(name,chunk);
	if( f == NULL )
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
	/* while lines begin with ' ' or '\t', add them */
	while( *f == ' ' || *f == '\t' ) {
		while( *f != '\0' && xisblank(*f) )
			f++;
		b = f;
		while( *f != '\0' && *f != '\n' )
			f++;
		e = f;
		while( e > b && *e != '\0' && xisspace(*e) )
			e--;
		if( !xisspace(*e) )
			v = strndup(b,e-b+1);
		else 
			v = strdup("");
		if( v == NULL ) {
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

retvalue chunk_getwholedata(const char *chunk,const char *name,char **value) {
	const char *f,*p,*e;
	bool_t afternewline = FALSE;
	char *v;

	f = chunk_getfield(name,chunk);
	if( f == NULL )
		return RET_NOTHING;
	for ( e = p = f ; *p != '\0' ; p++ ) {
		if( afternewline ) {
			if( *p == ' ' || *p == '\t' )
				afternewline = FALSE;
			else if( *p != '\r' )
				break;
		} else {
			if(  *p == '\n' ) {
				e = p;
				afternewline = TRUE;
			}
		}
	}
	if( !afternewline && *p == '\0' )
		e = p;
	v = strndup(f,e-f);
	if( v == NULL )
		return RET_ERROR_OOM;
	*value = v;
	return RET_OK;
}

retvalue chunk_getwordlist(const char *chunk,const char *name,struct strlist *strlist) {
	retvalue r;
	const char *f,*b;
	char *v;

	f = chunk_getfield(name,chunk);
	if( f == NULL )
		return RET_NOTHING;
	r = strlist_init(strlist);
	if( RET_WAS_ERROR(r) )
		return r;
	while( *f != '\0' ) {
		/* walk over spaces */
		while( *f != '\0' && xisspace(*f) ) {
			if( *f == '\n' ) {
				f++;
				if( *f != ' ' && *f != '\t' )
					return RET_OK;
			} else
				f++;
		}
		if( *f == '\0' )
			return RET_OK;
		b = f;
		/* search for end of word */
		while( *f != '\0' && !xisspace(*f) )
			f++;
		v = strndup(b,f-b);
		if( v == NULL ) {
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
	if( field == NULL )
		return RET_NOTHING;
	// TODO: check for things like Yes and No...

	return RET_OK;
}
/* return RET_OK, if field is found, RET_NOTHING, if not */ 
retvalue chunk_checkfield(const char *chunk,const char *name){
	const char *field;

	field = chunk_getfield(name,chunk);
	if( field == NULL )
		return RET_NOTHING;

	return RET_OK;
}

/* Parse a package/source-field: ' *value( ?\(version\))? *' */
retvalue chunk_getname(const char *chunk,const char *name,
		char **pkgname,bool_t allowversion) {
	const char *field,*name_end,*p;

	field = chunk_getfield(name,chunk);
	if( field == NULL )
		return RET_NOTHING;
	while( *field != '\0' && *field != '\n' && xisspace(*field) )
		field++;
	name_end = field;
	/* this has now checked somewhere else for correctness and
	 * is only a pure seperation process: 
	 * (as package(version) is possible, '(' must be checked) */
	while( *name_end != '\0' && *name_end != '\n' && *name_end != '(' && !xisspace(*name_end) )
		name_end++;
	p = name_end;
	while( *p != '\0' && *p != '\n' && xisspace(*p) )
		p++;
	if( name_end == field || 
		( *p != '\0' && *p != '\n' && 
		  ( !allowversion || *p != '('))) {
		if( *field == '\n' || *field == '\0' ) {
			fprintf(stderr,"Error: Field '%s' is empty!\n",name);
		} else {
			fprintf(stderr,"Error: Field '%s' contains unexpected character '%c'!\n",name,*p);
		}
		return RET_ERROR;
	}
	if( *p == '(' ) {
		while( *p != '\0' && *p != '\n' && *p != ')' )
			// TODO: perhaps check for wellformed version
			p++;
		if( *p != ')' ) {
			fprintf(stderr,"Error: Field '%s' misses closing parathesis!\n",name);
			return RET_ERROR;
		}
		p++;
	}
	while( *p != '\0' && *p != '\n' && xisspace(*p) )
		p++;
	if( *p != '\0' && *p != '\n' ) {
		fprintf(stderr,"Error: Field '%s' contains trailing junk starting with '%c'!\n",name,*p);
		return RET_ERROR;
	}

	*pkgname = strndup(field,name_end-field);
	if( *pkgname == NULL )
		return RET_ERROR_OOM;
	return RET_OK;

}

/* Parse a package/source-field: ' *value( ?\(version\))? *' */
retvalue chunk_getnameandversion(const char *chunk,const char *name,
		char **pkgname,char **version) {
	const char *field,*name_end,*p;
	char *v;

	field = chunk_getfield(name,chunk);
	if( field == NULL )
		return RET_NOTHING;
	while( *field != '\0' && *field != '\n' && xisspace(*field) )
		field++;
	name_end = field;
	/* this has now checked somewhere else for correctness and
	 * is only a pure seperation process: 
	 * (as package(version) is possible, '(' must be checked) */
	while( *name_end != '\0' && *name_end != '\n' && *name_end != '(' && !xisspace(*name_end) )
		name_end++;
	p = name_end;
	while( *p != '\0' && *p != '\n' && xisspace(*p) )
		p++;
	if( name_end == field || 
		( *p != '\0' && *p != '\n' && 
		   *p != '(')) {
		if( *field == '\n' || *field == '\0' ) {
			fprintf(stderr,"Error: Field '%s' is empty!\n",name);
		} else {
			fprintf(stderr,"Error: Field '%s' contains unexpected character '%c'!\n",name,*p);
		}
		return RET_ERROR;
	}
	if( *p == '(' ) {
		const char *version_begin;

		p++;
		while( *p != '\0' && *p != '\n' && xisspace(*p) )
			p++;
		version_begin = p;
		while( *p != '\0' && *p != '\n' && *p != ')'  && !xisspace(*p) )
			// TODO: perhaps check for wellformed version
			p++;
		v = strndup(version_begin,p-version_begin);
		if( v == NULL )
			return RET_ERROR_OOM;
		while( *p != '\0' && *p != '\n' && *p != ')'  && xisspace(*p) )
			p++;
		if( *p != ')' ) {
			free(v);
			if( *p == '\0' || *p == '\n' )
				fprintf(stderr,"Error: Field '%s' misses closing parathesis!\n",name);
			else
				fprintf(stderr,"Error: Field '%s' has multipe words after '('!\n",name);
			return RET_ERROR;
		}
		p++;
	} else {
		v = NULL;
	}
	while( *p != '\0' && *p != '\n' && xisspace(*p) )
		p++;
	if( *p != '\0' && *p != '\n' ) {
		free(v);
		fprintf(stderr,"Error: Field '%s' contains trailing junk starting with '%c'!\n",name,*p);
		return RET_ERROR;
	}

	*pkgname = strndup(field,name_end-field);
	if( *pkgname == NULL ) {
		free(v);
		return RET_ERROR_OOM;
	}
	*version = v;
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
	bool_t fieldsadded = FALSE;

	assert( chunk != NULL && beforethis != NULL );

	if( toadd == NULL )
		return NULL;

	c = chunk;

	/* calculate the maximal size we might end up with */
	size = 1+ strlen(c);
	f = toadd;
	while( f != NULL ) {
		if( f->data != NULL )
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
		if(!fieldsadded && strncmp(c,beforethis,len_beforethis) == 0 
				&& c[len_beforethis] == ':' ) {
			/* add them now: */
			f = toadd;
			while( f != NULL ) {
				if( f->data != NULL ) {
					memcpy(n,f->field,f->len_field);
					n += f->len_field;
					*n = ':'; n++;
					*n = ' '; n++;
					memcpy(n,f->data,f->len_data);
					n += f->len_data;
					*n = '\n'; n++;
				}
				f = f->next;
			}
			result = RET_OK;
			fieldsadded = TRUE;
		}
		/* is this one of the fields we added/will add? */
		f = toadd;
		while( f != NULL ) {
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
		} while( *ce == ' ' || *ce == '\t' );

		/* copy it, if it is not to be ignored */

		if( f == NULL && ce-c > 0) {
			// There are reports this might overwrite something,
			// though I neighter now how not why.
			memcpy(n,c,ce-c);
			n += ce-c;
		}

		/* and proceed with the next */
		c = ce;

	} while( *c != '\0' && *c != '\n' );

	*n = '\0';

	// If the problem still exists, I want to know it!
	assert( n-newchunk < 0 || (size_t)(n-newchunk) <= size-1 );

	if( result == RET_NOTHING ) {
		fprintf(stderr,"Could not find field '%s' in chunk '%s'!!!\n",beforethis,chunk);
		assert(FALSE);
	}

	return newchunk;
}

struct fieldtoadd *addfield_new(const char *field,const char *data,struct fieldtoadd *next) {
	struct fieldtoadd *n;

	assert(field != NULL && data != NULL);

	n = malloc(sizeof(struct fieldtoadd));
 	if( n == NULL ) {
 		addfield_free(next);
 		return NULL;
 	}
	n->field = field;
	n->len_field = strlen(field);
	n->data = data;
	n->len_data = strlen(data);
	n->next = next;
	return n;
}
struct fieldtoadd *deletefield_new(const char *field,struct fieldtoadd *next) {
	struct fieldtoadd *n;

	assert(field != NULL);

	n = malloc(sizeof(struct fieldtoadd));
 	if( n == NULL ) {
 		addfield_free(next);
 		return NULL;
 	}
	n->field = field;
	n->len_field = strlen(field);
	n->data = NULL;
	n->len_data = 0;
	n->next = next;
	return n;
}
struct fieldtoadd *addfield_newn(const char *field,const char *data,size_t len,struct fieldtoadd *next) {
	struct fieldtoadd *n;

	n = malloc(sizeof(struct fieldtoadd));
 	if( n == NULL ) {
 		addfield_free(next);
 		return NULL;
 	}
	n->field = field;
	n->len_field = strlen(field);
	n->data = data;
	n->len_data = len;
	n->next = next;
	return n;
}
void addfield_free(struct fieldtoadd *f) {
	struct fieldtoadd *g;

	while( f != NULL ) {
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

retvalue chunk_checkfields(const char *chunk,const char * const *allowedfieldnames,bool_t commentsallowed) {
	const char *c = chunk;

	assert( chunk != NULL );
	assert( allowedfieldnames != NULL );

	while( *c != '\0' ) {
		const char *nameend = c;

		if( *c == '#' && commentsallowed ) {
			while( *c != '\0' && *c != '\n' )
				c++;
			if( *c == '\0' )
				return RET_OK;
			c++;
			if( *c == ' ' ) {
				if( !IGNORING("Ignoring","To ignore this",
overlongcomments,"A comment is continued by a line starting with space in '%s'!\n",chunk) )
					return RET_ERROR;
			}
			do {
				while( *c != '\0' && *c != '\n' )
					c++;
				if( *c == '\0' )
					return RET_OK;
				c++;
			} while ( *c == ' ' );
			continue;
		}
		while( *nameend != '\0' && *nameend != '\n' && *nameend != ':' ) {
			nameend++;
		}
		if( *nameend != ':' ) {
			const char *p = c;
			while( xisspace(*p) && *p != '\0' && *p != '\n' ) {
				p++;
			}
			if( p == nameend ) {
				if( !IGNORING("Ignoring","To ignore this",spaceonlyline,
				"Line only containing spaces in '%s'!\n",chunk) )
					return RET_ERROR;
			} else {
				if( !IGNORING("Ignoring","To ignore this",malformedchunk,
				"Malformed chunk! Expecting colon in '%s'!\n",c) )
					return RET_ERROR;
			}

		} else {
			const char * const *allowedfn = allowedfieldnames;

			for( ; *allowedfn != NULL ; allowedfn++ ) {
				const char *allowed = *allowedfn;
				if( strncasecmp(allowed,c,nameend-c) == 0 
				    && allowed[nameend-c] == '\0' )
					break;
			}
			if( *allowedfn == NULL ) {
				char *fieldname = strndup(c,nameend-c);
				if( !IGNORING("Ignoring","To ignore this",
unknownfield, "Unknown field name: '%s'\n",(fieldname!=NULL)?fieldname:c) ) {
					free(fieldname);
					return RET_ERROR;
				}
				free(fieldname);
			}

		}
		c = nameend;

		do {
			while( *c != '\0' && *c != '\n' )
				c++;
			if( *c == '\0' )
				return RET_OK;
			c++;
			// TODO: whitespace or any space?
		} while ( *(c) == ' ' );

	}
	return RET_OK;
}
