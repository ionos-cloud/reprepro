/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007 Bernhard R. Link
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
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>
#include "error.h"
#include "chunks.h"
#include "sources.h"
#include "names.h"
#include "override.h"

struct overrideinfo {
	struct overrideinfo *next;
	char *packagename;
	struct strlist fields;
};

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
	if( last == NULL )
		return RET_ERROR_OOM;
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

retvalue override_read(const char *overridedir,const char *filename,struct overrideinfo **info) {
	struct overrideinfo *root = NULL ,*last = NULL;
	FILE *file;
	char buffer[1001];

	if( filename == NULL ) {
		*info = NULL;
		return RET_OK;
	}
	if( overridedir != NULL && filename[0] != '/' ) {
		char *fn = calc_dirconcat(overridedir,filename);
		if( fn == NULL )
			return RET_ERROR_OOM;
		file = fopen(fn,"r");
		free(fn);
	} else
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
				(void)fclose(file);
				return RET_ERROR;
			}
			fprintf(stderr,"Missing line-terminator in '%s'!\n",filename);
		} else {
			l--;
			buffer[l] = '\0';
		}
		while( l>0 && xisspace(buffer[l])) {
			buffer[l] = '\0';
			l--;
		}
		if( l== 0 )
			continue;
		p = buffer;
		while( *p !='\0' && xisspace(*p) )
			*(p++)='\0';
		firstpart = p;
		while( *p !='\0' && !xisspace(*p) )
			p++;
		while( *p !='\0' && xisspace(*p) )
			*(p++)='\0';
		secondpart = p;
		while( *p !='\0' && !xisspace(*p) )
			p++;
		while( *p !='\0' && xisspace(*p) )
			*(p++)='\0';
		thirdpart = p;
		if( last == NULL || ( strcmp(last->packagename,firstpart) > 0 &&
			       strcmp(root->packagename,firstpart) > 0 )) {
			/* adding in front of it */
			r = newoverrideinfo(firstpart,secondpart,thirdpart,&root);
			if( RET_WAS_ERROR(r) ) {
				(void)fclose(file);
				return r;
			}
			last = root;
			continue;
		} else {
			if( strcmp(last->packagename,firstpart) > 0 )
				last = root;
			if( strcmp(last->packagename,firstpart) < 0 ) {
				while( last->next != NULL &&
					strcmp(last->next->packagename,firstpart) < 0) {
					last = last->next;
				}
				if( last->next == NULL ||
				    strcmp(last->next->packagename,firstpart) != 0 ) {
					/* add it after last and before last->next */
					r = newoverrideinfo(firstpart,secondpart,thirdpart,&last->next);
					last = last->next;
					if( RET_WAS_ERROR(r) ) {
						(void)fclose(file);
						return r;
					}
					continue;
				} else
					last = last->next;

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
			(void)fclose(file);
			return r;
		}
	}
	(void)fclose(file);
	*info = root;
	if( root == NULL )
		return RET_NOTHING;
	else
		return RET_OK;
}

const struct overrideinfo *override_search(const struct overrideinfo *overrides,const char *package) {
	int c;

	while( overrides != NULL ) {
		c = strcmp(overrides->packagename,package);
		if( c < 0 )
			overrides = overrides->next;
		else if( c == 0 )
			return overrides;
		else
			return NULL;
	}
	return NULL;
}

const char *override_get(const struct overrideinfo *override,const char *field) {
	int i;

	if( override == NULL )
		return NULL;

	for( i = 0 ; i+1 < override->fields.count ; i+=2 ) {
		// TODO curently case-sensitiv. warn if otherwise?
		if( strcmp(override->fields.values[i],field) == 0 )
			return override->fields.values[i+1];
	}
	return NULL;
}

/* add new fields to otherreplaces, but not "Section", or "Priority".
 * incorporates otherreplaces, or frees them on error,
 * returns otherreplaces when nothing was to do, NULL on RET_ERROR_OOM*/
struct fieldtoadd *override_addreplacefields(const struct overrideinfo *override,
		struct fieldtoadd *otherreplaces) {
	int i;

	if( override == NULL )
		return otherreplaces;

	for( i = 0 ; i+1 < override->fields.count ; i+=2 ) {
		if( strcmp(override->fields.values[i],SECTION_FIELDNAME) != 0 &&
		    strcmp(override->fields.values[i],PRIORITY_FIELDNAME) != 0 ) {
			otherreplaces = addfield_new(
				override->fields.values[i],override->fields.values[i+1],
				otherreplaces);
			if( otherreplaces == NULL )
				return NULL;
		}

	}
	return otherreplaces;

}

