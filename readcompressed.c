/*  This file is part of "reprepro"
 *  Copyright (C) 2008 Bernhard R. Link
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
#include <limits.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#endif

#include "error.h"
#include "readcompressed.h"

extern int verbose;

struct readcompressed {
	// preliminary, as dead slow...
	gzFile f;
	char buffer[4097];
	size_t lineno;
};

bool readcompressed_getline(struct readcompressed *r, /*@out@*/const char **line) {
	char *c;

	r->lineno++;
	if( gzgets(r->f, r->buffer, 4096) == NULL )
		return false;
	r->buffer[4096] = '\0';
	c = strchr(r->buffer, '\n');
	if( c == NULL )
		return false;
	*c = '\0';
	while( --c > r->buffer && *c == '\r' )
		*c = '\0';
	*line = r->buffer;
	return true;
}

char readcompressed_overlinegetchar(struct readcompressed *r) {
	char *c, ch;

	r->lineno++;
	if( gzgets(r->f, r->buffer, 4096) == NULL )
		return false;
	r->buffer[4096] = '\0';
	ch = r->buffer[0];
	if( ch == '\n' ) {
		return '\0';
	}
	c = strchr(r->buffer, '\n');
	while( c == NULL ) {
		if( gzgets(r->f, r->buffer, 4096) == NULL )
			return ch;
		c = strchr(r->buffer, '\n');
	}
	return ch;
}

retvalue readcompressed_open(/*@out@*/struct readcompressed **r, const char *filename, enum compression c) {
	struct readcompressed *n;

	if( c != c_uncompressed && c != c_gzipped )
		return RET_NOTHING;
	n = calloc(1, sizeof(struct readcompressed));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->f = gzopen(filename, "r");
	if( n->f == NULL ) {
		free(n);
		return RET_NOTHING;
	}
	*r = n;
	return RET_OK;
}

retvalue readcompressed_close(struct readcompressed *r) {
	if( r != NULL ) {
		gzclose(r->f);
		free(r);
	}
	// TODO: implement error handling...
	return RET_OK;
}

void readcompressed_abort(struct readcompressed *r) {
	if( r != NULL ) {
		if( verbose > 20 ) {
			fprintf(stderr, "Aborted reading compessed file at line %llu\n",
					(long long unsigned int)r->lineno);
		}
		gzclose(r->f);
		free(r);
	}
}
