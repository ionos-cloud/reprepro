/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "filterlist.h"

extern int verbose;

struct filterlistfile {
	size_t reference_count;

	char *filename;
	size_t filename_len;

	/*@owned@*//*@null@*/
	struct filterlistitem *root;
	/*@dependent@*//*@null@*/
	const struct filterlistitem *last;

	/*@owned@*//*@null@*/
	struct filterlistfile *next;
} *listfiles = NULL;

struct filterlistitem {
	/*@owned@*//*@null@*/
	struct filterlistitem *next;
	char *packagename;
	enum filterlisttype what;
};

static void filterlistitems_free(/*@null@*//*@only@*/struct filterlistitem *list) {
	while( list != NULL ) {
		struct filterlistitem *next = list->next;
		free(list->packagename);
		free(list);
		list = next;
	}
}

static void filterlistfile_unlock(struct filterlistfile *list) {
	assert( list != NULL );

	if( list->reference_count <= 1 ) {
		struct filterlistfile **p = &listfiles;

		assert( list->reference_count == 1 );
		if( list->reference_count == 0 )
			return;

		while( *p != NULL && *p != list )
			p = &(*p)->next;
		assert( p != NULL );
		if( *p == list ) {
			*p = list->next;
			filterlistitems_free(list->root);
			free(list->filename);
			free(list);
		}
	} else
		list->reference_count--;
}

static inline retvalue filterlistfile_parse(struct filterlistfile *n, const char *filename, FILE *f) {
	char *lineend,*namestart,*nameend,*what;
	int cmp;
	enum filterlisttype type;
	struct filterlistitem *h;
	char line[1001];
	int lineno = 0;
	struct filterlistitem **last = &n->root;

	while( fgets(line,1000,f) != NULL ) {
		lineno++;
		lineend = strchr(line,'\n');
		if( lineend == NULL ) {
			fprintf(stderr,"Overlong line in '%s'!\n",filename);
			return RET_ERROR;
		}
		while( lineend >= line && xisspace(*lineend) )
			*(lineend--) = '\0';
		/* Ignore line only containing whitespace */
		if( line[0] == '\0' )
			continue;
		namestart = line;
		while( *namestart != '\0' && xisspace(*namestart) )
			namestart++;
		nameend=namestart;
		while( *nameend != '\0' && !xisspace(*nameend) )
			nameend++;
		what = nameend;
		while( *what != '\0' && xisspace(*what) )
			*(what++)='\0';
		if( *what == '\0' ) {
			fprintf(stderr,"Malformed line in '%s': %d!\n",filename,lineno);
			return RET_ERROR;
		}
		if( strcmp(what,"install") == 0 ) {
			type = flt_install;
		} else if( strcmp(what,"deinstall") == 0 ) {
			type = flt_deinstall;
		} else if( strcmp(what,"purge") == 0 ) {
			type = flt_purge;
		} else if( strcmp(what,"hold") == 0 ) {
			type = flt_hold;
		} else if( strcmp(what,"error") == 0 ) {
			type = flt_error;
		} else {
			fprintf(stderr,"Unknown status in '%s':%d: '%s'!\n",filename,lineno,what);
			return RET_ERROR;
		}
		if( *last == NULL || strcmp(namestart,(*last)->packagename) < 0 )
			last = &n->root;
		cmp = -1;
		while( *last != NULL && (cmp=strcmp(namestart,(*last)->packagename)) > 0 )
			last = &((*last)->next);
		if( cmp == 0 ) {
			fprintf(stderr,"Two lines describing '%s' in '%s'!\n",namestart,filename);
			return RET_ERROR;
		}
		h = calloc(1,sizeof(*h));
		if( h == NULL ) {
			return RET_ERROR_OOM;
		}
		h->next = *last;
		*last = h;
		h->what = type;
		h->packagename = strdup(namestart);
		if( h->packagename == NULL ) {
			return RET_ERROR_OOM;
		}
	}
	n->last = *last;
	return RET_OK;

}

static inline retvalue filterlistfile_read(struct filterlistfile *n, const char *filename) {
	FILE *f;
	retvalue r;

	f = fopen(filename,"r");
	if( f == NULL ) {
		fprintf(stderr,"Cannot open %s for reading: %m!\n",filename);
		return RET_ERROR;
	}
	r = filterlistfile_parse(n, filename, f);

	// Can this return an yet unseen error? was read-only..
	(void)fclose(f);
	return r;
}

static inline retvalue filterlistfile_get(const char *confdir, const char *filename, size_t len, struct filterlistfile **list) {
	struct filterlistfile *p;
	retvalue r;

	for( p = listfiles ; p != NULL ; p = p->next ) {
		if( p->filename_len == len &&
				strncmp(p->filename, filename, len) == 0 ) {
			p->reference_count++;
			*list = p;
			return RET_OK;
		}
	}
	p = calloc(1,sizeof(struct filterlistfile));
	if( p == NULL )
		return RET_ERROR_OOM;
	p->reference_count = 1;
	p->filename = strndup(filename, len);
	p->filename_len = len;
	if( p->filename == NULL ) {
		free(p);
		return RET_ERROR_OOM;
	}
	if( p->filename[0] != '/' ) {
		const char *fullfilename = calc_dirconcat(confdir, p->filename);
		if( fullfilename == NULL )
			r = RET_ERROR_OOM;
		else {
			r = filterlistfile_read(p, fullfilename);
			free(fullfilename);
		}
	} else
		r = filterlistfile_read(p, p->filename);

	if( RET_IS_OK(r) ) {
		p->next = listfiles;
		listfiles = p;
		*list = p;
	} else {
		filterlistitems_free(p->root);
		free(p->filename);
		free(p);
	}
	return r;
}

void filterlist_release(struct filterlist *list) {
	size_t i;

	assert(list != NULL);

	if( list->files != NULL ) {
		for( i = 0 ; i < list->count ; i++ )
			filterlistfile_unlock(list->files[i]);
		free(list->files);
		list->files = NULL;
	} else {
		assert( list->count == 0 );
	}
}

retvalue filterlist_load(struct filterlist *list, const char *confdir,const char *configline) {
	const char *filename;
	enum filterlisttype defaulttype;
	size_t count;
	struct filterlistfile **files;


	if( strncmp(configline,"install",7) == 0 && xisspace(configline[7]) ) {
		defaulttype = flt_install; filename = configline + 7;
	} else if( strncmp(configline,"hold",4) == 0 && xisspace(configline[4]) ) {
		defaulttype = flt_hold; filename = configline + 4;
	} else if( strncmp(configline,"deinstall",9) == 0 && xisspace(configline[9]) ) {
		defaulttype = flt_deinstall; filename = configline + 9;
	} else if( strncmp(configline,"purge",5) == 0 && xisspace(configline[5]) ) {
		defaulttype = flt_purge; filename = configline + 5;
	} else if( strncmp(configline,"error",5) == 0 && xisspace(configline[5]) ) {
		defaulttype = flt_error; filename = configline + 5;
	} else {
		fprintf(stderr,"Cannot parse '%s' into the format 'install|deinstall|purge|hold <filename>'\n",configline);
		return RET_ERROR;
	}
	count = 0;
	files = NULL;
	while( *filename != '\0' && xisspace(*filename) )
		filename++;
	while( *filename != '\0' ) {
		const char *filenameend = filename;
		struct filterlistfile **n;
		retvalue r;

		while( *filenameend != '\0' && !xisspace(*filenameend) )
			filenameend++;

		n = realloc(files, (count+1)*
				sizeof(struct filterlistfile *));
		if( n == NULL ) {
			r = RET_ERROR_OOM;
		} else {
			n[count] = NULL;
			files = n;
			r = filterlistfile_get(confdir, filename, filenameend-filename, &files[count]);
			if( RET_IS_OK(r) )
				count++;
		}
		if( RET_WAS_ERROR(r) ) {
			while( count > 0 ) {
				count--;
				filterlistfile_unlock(files[count]);
			}
			free(files);
			return r;
		}
		filename = filenameend;
		while( *filename != '\0' && xisspace(*filename) )
			filename++;
	}
	list->count = count;
	list->files = files;
	list->defaulttype = defaulttype;
	return RET_OK;
}

void filterlist_empty(struct filterlist *list, enum filterlisttype defaulttype) {
	list->files = NULL;
	list->count = 0;
	list->defaulttype = defaulttype;
}

static inline bool_t find(const char *name,/*@null@*/struct filterlistfile *list) {
	int cmp;
	/*@dependent@*/const struct filterlistitem *last = list->last;

	assert( last != NULL );

	if( last->next != NULL ) {
		cmp = strcmp(name,last->next->packagename);
		if( cmp == 0 ) {
			list->last = last->next;
			return TRUE;
		}
	}
	if( last->next == NULL || cmp < 0 ) {
		cmp = strcmp(name,last->packagename);
		if( cmp == 0 ) {
			return TRUE;
		} else if( cmp > 0 )
			return FALSE;
		last = list->root;
		cmp = strcmp(name,last->packagename);
		if( cmp == 0 ) {
			list->last = list->root;
			return TRUE;
		} else if( cmp < 0 )
			return FALSE;
	}
	/* now we are after last */
	while( last->next != NULL ) {
		cmp = strcmp(name,last->next->packagename);
		if( cmp == 0 ) {
			list->last = last->next;
			return TRUE;
		}
		if( cmp < 0 ) {
			list->last = last;
			return FALSE;
		}
		last = last->next;
	}
	list->last = last;
	return FALSE;
}

enum filterlisttype filterlist_find(const char *name,struct filterlist *list) {
	size_t i;
	for( i = 0 ; i < list->count ; i++ ) {
		if( list->files[i]->root != NULL && find(name,list->files[i]) )
			return list->files[i]->last->what;
	}
	return list->defaulttype;
}
