/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
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

static void filterlistitems_free(/*@null@*//*@only@*/struct filterlistitem *list) {
	while( list ) {
		struct filterlistitem *next = list->next;
		free(list->packagename);
		free(list);
		list = next;
	}
}

void filterlist_release(struct filterlist *list) {
	assert(list != NULL);

	filterlistitems_free(list->root);
}
	

retvalue filterlist_load(struct filterlist *list, const char *confdir,const char *configline) {
	char *fullfilename;
	const char *filename;
	FILE *f;
	char line[1001];
	struct filterlistitem *root,**last = &root;
	int lineno = 0;
	enum filterlisttype defaulttype;

	root = NULL;

	if( strncmp(configline,"install",7) == 0 && isspace(configline[7]) ) {
		defaulttype = flt_install; filename = configline + 7;
	} else if( strncmp(configline,"hold",4) == 0 && isspace(configline[4]) ) {
		defaulttype = flt_hold; filename = configline + 4;
	} else if( strncmp(configline,"deinstall",9) == 0 && isspace(configline[9]) ) {
		defaulttype = flt_hold; filename = configline + 9;
	} else if( strncmp(configline,"purge",5) == 0 && isspace(configline[5]) ) {
		defaulttype = flt_hold; filename = configline + 5;
	} else if( strncmp(configline,"error",5) == 0 && isspace(configline[5]) ) {
		defaulttype = flt_error; filename = configline + 5;
	} else {
		fprintf(stderr,"Cannot parse '%s' into the format 'install|deinstall|purge|hold <filename>'\n",configline);
		return RET_ERROR;
	}
	while( *filename != '\0' && isspace(*filename) )
		filename++;
	if( filename[0] != '/' ) {
		fullfilename = calc_dirconcat(confdir,filename);
		if( fullfilename == NULL )
			return RET_ERROR_OOM;
		filename = fullfilename;
	} else
		fullfilename = NULL;

	f = fopen(filename,"r");
	if( f == NULL ) {
		fprintf(stderr,"Cannot open %s for reading: %m!\n",filename);
		free(fullfilename);
		return RET_ERROR;
	}
	while( fgets(line,1000,f) != NULL ) {
		char *lineend,*namestart,*nameend,*what;
		int cmp;
		enum filterlisttype type;
		struct filterlistitem *h;

		lineno++;
		lineend = strchr(line,'\n');
		if( lineend == NULL ) {
			fprintf(stderr,"Overlong line in '%s'!\n",filename);
			free(fullfilename);
			(void)fclose(f);
			filterlistitems_free(root);
			return RET_ERROR;
		}
		while( lineend >= line && isspace(*lineend) )
			*(lineend--) = '\0';
		/* Ignore line only containing whitespace */
		if( line[0] == '\0' )
			continue;
		namestart = line;
		while( *namestart != '\0' && isspace(*namestart) )
			namestart++;
		nameend=namestart;
		while( *nameend != '\0' && !isspace(*nameend) )
			nameend++;
		what = nameend;
		while( *what != '\0' && isspace(*what) )
			*(what++)='\0';
		if( *what == '\0' ) {
			fprintf(stderr,"Malformed line in '%s': %d!\n",filename,lineno);
			free(fullfilename);
			(void)fclose(f);
			filterlistitems_free(root);
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
			free(fullfilename);
			(void)fclose(f);
			filterlistitems_free(root);
			return RET_ERROR;
		}
		if( *last == NULL || strcmp(namestart,(*last)->packagename) < 0 )
			last = &root;
		cmp = -1;
		while( *last != NULL && (cmp=strcmp(namestart,(*last)->packagename)) > 0 )
			last = &((*last)->next);
		if( cmp == 0 ) {
			fprintf(stderr,"Two lines describing '%s' in '%s'!\n",namestart,filename);
			free(fullfilename);
			(void)fclose(f);
			filterlistitems_free(root);
			return RET_ERROR;
		}
		h = calloc(1,sizeof(*h));
		if( h == NULL ) {
			free(fullfilename);
			(void)fclose(f);
			filterlistitems_free(root);
			return RET_ERROR_OOM;
		}
		h->next = *last;
		*last = h;
		h->what = type;
		h->packagename = strdup(namestart);
		if( h->packagename == NULL ) {
			free(fullfilename);
			(void)fclose(f);
			filterlistitems_free(root);
			return RET_ERROR_OOM;
		}
	}
	free(fullfilename);
	// Can this be an error? was read-only..
	fclose(f);
	list->root = root;
	list->last = root;
	list->defaulttype = defaulttype;
	return RET_OK;
}

void filterlist_empty(struct filterlist *list, enum filterlisttype defaulttype) {
	list->root = NULL;
	list->last = NULL;
	list->defaulttype = defaulttype;
}

static inline bool_t find(const char *name,/*@null@*/struct filterlist *list) {
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
	if( list->root != NULL && find(name,list) ) {
		return list->last->what;
	} else {
		return list->defaulttype;
	}
}
