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
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "dirs.h"

extern int verbose;

/* create directory dirname. returns 0 on success or if already existing, -1 otherwise */
static int check_dir(const char *dirname) {
	int ret;

	ret = mkdir(dirname,0770);
	if( ret == 0 ) {
		if( verbose )
		fprintf(stderr,"Created directory \"%s\"\n",dirname);
		return 0;
	} else if( ret < 0 && errno != EEXIST ) {
		fprintf(stderr,"Can not create directory \"%s\": %m\n",dirname);
		return -1;
	}
	return 0;
}

/* create recursively all parent directories before the last '/' */
int make_parent_dirs(const char *filename) {
	const char *p;
	char *h;
	int i;

	for( p = filename+1, i = 1 ; *p ; p++,i++) {
		if( *p == '/' ) {
			h = strndup(filename,i);
			if( check_dir(h) < 0 ) {
				free(h);
				return -1;
			}
			free(h);
		}
	}
	return 0;
}

/* create dirname and any '/'-seperated part of it */
int make_dir_recursive(const char *dirname) {
	make_parent_dirs(dirname);
	return check_dir(dirname);
}
