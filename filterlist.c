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

void filterlist_free(struct filterlist *list) {
	while( list ) {
		struct filterlist *next = list->next;
		free(list->packagename);
		free(list);
		list = next;
	}
}

retvalue filterlist_load(struct filterlist **list, const char *confdir, const char *filename) {
	char *fullfilename;
	FILE *f;
	char line[1001];
	struct filterlist *root,**last = &root;
	int lineno = 0;

	root = NULL;

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
		struct filterlist *h;

		lineno++;
		lineend = strchr(line,'\n');
		if( lineend == NULL ) {
			fprintf(stderr,"Overlong line in '%s'!\n",filename);
			free(fullfilename);
			filterlist_free(root);
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
			filterlist_free(root);
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
		} else {
			fprintf(stderr,"Unknown status in '%s':%d: '%s'!\n",filename,lineno,what);
			free(fullfilename);
			filterlist_free(root);
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
			filterlist_free(root);
			return RET_ERROR;
		}
		h = calloc(1,sizeof(*h));
		h->next = *last;
		*last = h;
		h->what = type;
		h->packagename = strdup(namestart);
		if( h->packagename == NULL ) {
			free(fullfilename);
			filterlist_free(root);
			return RET_ERROR_OOM;
		}
	}
	if( root == NULL ) {
		/* to make a empty file different from no file */
		root = calloc(1,sizeof(*root));
		root->next = NULL;
		root->what = flt_deinstall;
		root->packagename = strdup("__%%%%%%__");
		if( root->packagename == NULL ) {
			free(fullfilename);
			filterlist_free(root);
			return RET_ERROR_OOM;
		}
	}
	*list = root;
	return RET_OK;
}

bool_t filterlist_find(const char *name,struct filterlist *root,const struct filterlist **last_p) {
	int cmp;
	const struct filterlist *last = *last_p;

	assert( last != NULL );

	if( last->next != NULL ) {
		cmp = strcmp(name,last->next->packagename);
		if( cmp == 0 ) {
			*last_p = last->next;
			return TRUE;
		}
	}
	if( last->next == NULL || cmp < 0 ) {
		cmp = strcmp(name,last->packagename);
		if( cmp == 0 ) {
			return TRUE;
		} else if( cmp > 0 )
			return FALSE;
		last = root;
		cmp = strcmp(name,last->packagename);
		if( cmp == 0 ) {
			*last_p = root;
			return TRUE;
		} else if( cmp < 0 )
			return FALSE;
	}
	/* now we are after last */
	while( last->next != NULL ) {
		cmp = strcmp(name,last->next->packagename);
		if( cmp == 0 ) {
			*last_p = last->next;
			return TRUE;
		}
		if( cmp < 0 ) {
			*last_p = last;
			return FALSE;
		}
		last = last->next;
	}
	*last_p = last;
	return FALSE;
}
