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
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>
#include <db.h>
#include "error.h"
#include "chunks.h"
#include "sources.h"
#include "names.h"
#include "override.h"

/*
struct overrideinfo {
	struct overrideinfo *next;
	char *packagename;
	struct strlist fields;
};
*/

void override_free(struct overrideinfo *info) {
	struct overrideinfo *i;
	
	while( (i = info) != NULL ) {
		if( i == NULL )
			return;
		strlist_done(&i->fields);
		free(i->packagename);
		info = i->next;
		free(i);
	}
}

static inline retvalue newoverrideinfo(const char *firstpart,const char *secondpart,const char *thirdpart,struct overrideinfo **info) {
	struct overrideinfo *last;
	retvalue r;
	char *p;

	last = calloc(1,sizeof(struct overrideinfo));
	last->packagename=strdup(firstpart);
	r = strlist_init_n(6,&last->fields);
	if( RET_WAS_ERROR(r) ) {
		override_free(last);
		return r;
	}
	p = strdup(secondpart);
	if( p == NULL )
		r = RET_ERROR_OOM;
	else
		r = strlist_add(&last->fields,p);
	if( !RET_WAS_ERROR(r) ) {
		p = strdup(thirdpart);
		if( p == NULL )
			r = RET_ERROR_OOM;
		else
			r = strlist_add(&last->fields,p);
	}
	if( RET_WAS_ERROR(r) ) {
		override_free(last);
		return r;
	}
	last->next = *info;
	*info = last;
	return RET_OK;
}

retvalue override_read(const char *filename,struct overrideinfo **info) {
	struct overrideinfo *root = NULL ,*last = NULL;
	FILE *file;
	char buffer[1001];

	file = fopen(filename,"r");
	if( file == NULL ) {
		int e = errno;
		fprintf(stderr,"Error opening override file '%s': %m\n",filename);
		return RET_ERRNO(e);
	}
	while( fgets(buffer,1000,file) != NULL ){
		retvalue r;
		const char *firstpart,*secondpart,*thirdpart;
		char *p;
		size_t l = strlen(buffer);

		if( buffer[l-1] != '\n' ) {
			if( l >= 999 ) {
				fprintf(stderr,"Too long line in '%s'!\n",filename);
				fclose(file);
				return RET_ERROR;
			}
			fprintf(stderr,"Missing line-terminator in '%s'!\n",filename);
		} else {
			l--;
			buffer[l] = '\0';
		}
		while( l>0 && isspace(buffer[l])) {
			buffer[l] = '\0';
			l--;
		}
		if( l== 0 )
			continue;
		p = buffer;
		while( *p && isspace(*p) )
			*(p++)='\0';
		firstpart = p;
		while( *p && !isspace(*p) )
			p++;
		while( *p && isspace(*p) )
			*(p++)='\0';
		secondpart = p;
		while( *p && !isspace(*p) )
			p++;
		while( *p && isspace(*p) )
			*(p++)='\0';
		thirdpart = p;
		if( !last || ( strcmp(last->packagename,firstpart) > 0 &&
			       strcmp(root->packagename,firstpart) > 0 )) {
			/* adding in front of it */
			r = newoverrideinfo(firstpart,secondpart,thirdpart,&root);
			if( RET_WAS_ERROR(r) )
				return r;
			last = root;
			continue;
		} else {
			if( strcmp(last->packagename,firstpart) > 0 )
				last = root;
			if( strcmp(last->packagename,firstpart) < 0 ) {
				while( last->next && 
					strcmp(last->next->packagename,firstpart) > 0) {
					last = last->next;
				}
				/* add it after last and before last->next */
				r = newoverrideinfo(firstpart,secondpart,thirdpart,&last->next);
				if( RET_WAS_ERROR(r) )
					return r;
				continue;
			} else {
				assert( strcmp(last->packagename,firstpart)  == 0 );
			}
		}
		p = strdup(secondpart);
		if( p == NULL )
			r = RET_ERROR_OOM;
		else
			r = strlist_add(&last->fields,p);
		if( !RET_WAS_ERROR(r) ) {
			p = strdup(thirdpart);
			if( p == NULL )
				r = RET_ERROR_OOM;
			else
				r = strlist_add(&last->fields,p);
		}
		if( RET_WAS_ERROR(r) ) {
			override_free(root);
			return r;
		}
	}
	*info = root;
	if( root == NULL )
		return RET_NOTHING;
	else
		return RET_ERROR;
}
