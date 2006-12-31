/*  This file is part of "reprepro"
 *  Copyright (C) 2003 Bernhard R. Link
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
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "error.h"
#include "strlist.h"
#include "dirs.h"
#include "names.h"

extern int verbose;

/* create directory dirname. */
static retvalue dirs_check(const char *dirname) {
	int ret;

	ret = mkdir(dirname,0775);
	if( ret == 0 ) {
		if( verbose > 0)
			fprintf(stderr,"Created directory \"%s\"\n",dirname);
		return RET_OK;
	} else if( ret < 0 && errno != EEXIST ) {
		fprintf(stderr,"Can not create directory \"%s\": %m\n",dirname);
		return RET_ERROR;
	}
	return RET_NOTHING;
}

/* create recursively all parent directories before the last '/' */
retvalue dirs_make_parent(const char *filename) {
	const char *p;
	char *h;
	int i;
	retvalue r;

	for( p = filename+1, i = 1 ; *p != '\0' ; p++,i++) {
		if( *p == '/' ) {
			h = strndup(filename,i);
			r = dirs_check(h);
			if( RET_WAS_ERROR(r) ) {
				free(h);
				return r;
			}
			free(h);
		}
	}
	return RET_OK;
}

/* create dirname and any '/'-separated part of it */
retvalue dirs_make_recursive(const char *directory) {
	retvalue r,result;

	if( interrupted() ) {
		return RET_ERROR_INTERUPTED;
	}
	r = dirs_make_parent(directory);
	result = dirs_check(directory);
	RET_UPDATE(result,r);
	return result;
}

retvalue dirs_getdirectory(const char *filename,char **directory) {
	size_t len;

	assert( filename != NULL && *filename != '\0' );

	len = strlen(filename);
	while( len > 1 && filename[len-1] == '/' ) {
		len--;
	}
	while( len > 0 && filename[len-1] != '/' ) {
		len--;
	}
	if( len == 0 ) {
		*directory = strdup(".");
	} else {
		if( len == 1 )
			*directory = strdup("/");
		else
			*directory = strndup(filename,len-1);
	}
	if( *directory == NULL )
		return RET_ERROR_OOM;
	else
		return RET_OK;

}
const char *dirs_basename(const char *filename) {
	const char *bn;

	bn = strrchr(filename,'/');
	if( bn == NULL )
		return filename;
	// not really suited for the basename of directories,
	// things like /bla/blub/ will give emtpy string...
	return bn+1;
}
