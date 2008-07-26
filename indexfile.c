/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007,2008 Bernhard R. Link
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
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <zlib.h>
#include <assert.h>
#include "error.h"
#include "chunks.h"
#include "names.h"
#include "indexfile.h"

/* the purpose of this code is to read index files, either from a snapshot
 * previously generated or downloaded while updating. */

struct indexfile {
	gzFile f;
	char *filename;
	int linenumber, startlinenumber;
	retvalue status;
	char *buffer;
	size_t size;
};

extern int verbose;

retvalue indexfile_open(struct indexfile **file_p, const char *filename) {
	struct indexfile *f = calloc(1, sizeof(struct indexfile));

	if( FAILEDTOALLOC(f) )
		return RET_ERROR_OOM;
	f->filename = strdup(filename);
	if( FAILEDTOALLOC(f->filename) ) {
		free(f);
		return RET_ERROR_OOM;
	}
	// TODO: gzopen and gzgets are slow like hell, improve !
	f->f = gzopen(filename, "r");
	if( f->f == NULL ) {
		fprintf(stderr, "Unable to open file %s: %s\n",
				filename, strerror(errno));
		free(f->filename);
		free(f);
		return RET_ERRNO(errno);
	}
	f->linenumber = 0;
	f->startlinenumber = 0;
	f->status = RET_OK;
	f->size = 4096;
	f->buffer = malloc(f->size);
	if( FAILEDTOALLOC(f->buffer) ) {
		(void)gzclose(f->f);
		free(f);
		return RET_ERROR_OOM;
	}
	*file_p = f;
	return RET_OK;
}

retvalue indexfile_close(struct indexfile *f) {
	retvalue r;

	//TODO: check result:
	gzclose(f->f);

	free(f->filename);
	free(f->buffer);
	r = f->status;
	free(f);

	return r;
}

//TODO: this should now also be able to parse \r\n terminated lines instead
// of only \n terminated oned. Though this has still to be tested properly...
//
//TODO: as said above, gzgets is too slow...

static retvalue indexfile_get(struct indexfile *f) {
	char *bhead;
	size_t already, without, l;
	bool afternewline = false;

	already = 0; without = 0;
	bhead = f->buffer;
	f->startlinenumber = f->linenumber + 1;
	while( gzgets(f->f, bhead, f->size-1-already) != NULL ) {
		f->linenumber++;
		char *p;
		p = bhead;
		while( *p != '\0' ) {
			if( *p != '\r' && *p != '\n' )
				without = 1 + p - f->buffer;
			p++;
		}
		if( without == 0 ) {
			/* ignore leading newlines... */
			bhead = f->buffer;
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
		if( f->size-already < 1024 ) {
			char *n;
			if( f->size >= 513*1024*1024 ) {
				fprintf(stderr, "Will not process chunks larger than half a gigabyte!\n");
				return RET_ERROR;
			}
			n = realloc(f->buffer, f->size*2 );
			if( FAILEDTOALLOC(n) )
				return RET_ERROR_OOM;
			f->buffer = n;
			f->size *= 2;
		}
		bhead = f->buffer + already;
	}
	if( without == 0 )
		return RET_NOTHING;
	assert( without < f->size );
	/* we do not want to include the final newlines */
	f->buffer[without] = '\0';
	return RET_OK;
}

bool indexfile_getnext(struct indexfile *f, char **name_p, char **version_p, const char **control_p, const struct target *target, bool allowwrongarchitecture) {
	retvalue r;
	bool ignorecruft = false; // TODO
	char *packagename, *version, *architecture;
	const char *control;

	packagename = NULL; version = NULL;
	do {
		free(packagename); packagename = NULL;
		free(version); version = NULL;
		r = indexfile_get(f);
		if( !RET_IS_OK(r) )
			break;
		control = f->buffer;
		r = chunk_getvalue(control, "Package", &packagename);
		if( r == RET_NOTHING ) {
			fprintf(stderr,
"Error parsing %s line %d to %d: Chunk without 'Package:' field!\n",
					f->filename,
					f->startlinenumber, f->linenumber);
			if( !ignorecruft )
				r = RET_ERROR_MISSING;
			else
				continue;
		}
		if( RET_WAS_ERROR(r) )
			break;

		r = chunk_getvalue(control, "Version", &version);
		if( r == RET_NOTHING ) {
			fprintf(stderr,
"Error parsing %s line %d to %d: Chunk without 'Version:' field!\n",
					f->filename,
					f->startlinenumber, f->linenumber);
			if( !ignorecruft )
				r = RET_ERROR_MISSING;
			else
				continue;
		}
		if( RET_WAS_ERROR(r) )
			break;
		r = chunk_getvalue(control, "Architecture", &architecture);
		if( RET_WAS_ERROR(r) )
			break;
		if( r == RET_NOTHING )
			architecture = NULL;
		if( strcmp(target->packagetype, "dsc") == 0 ) {
			free(architecture);
		} else {
			/* check if architecture fits for target and error
			    out if not ignorewrongarchitecture */
			if( architecture == NULL ) {
				fprintf(stderr,
"Error parsing %s line %d to %d: Chunk without 'Architecture:' field!\n",
						f->filename,
						f->startlinenumber, f->linenumber);
				if( !ignorecruft ) {
					r = RET_ERROR_MISSING;
					break;
				} else
					continue;
			} else if( strcmp(architecture, "all") != 0 &&
			           strcmp(architecture,
						target->architecture) != 0) {
				if( allowwrongarchitecture ) {
					free(architecture);
					continue;
				} else {
					fprintf(stderr,
"Error parsing %s line %d to %d: Wrong 'Architecture:' field '%s' (need 'all' or '%s')!\n",
						f->filename,
						f->startlinenumber, f->linenumber,
						architecture,
						target->architecture);
					r = RET_ERROR;
				}
			}
			free(architecture);
		}
		if( RET_WAS_ERROR(r) )
			break;
		*control_p = control;
		*name_p = packagename;
		*version_p = version;
		return true;
	} while( true );
	free(packagename);
	free(version);
	RET_UPDATE(f->status, r);
	return false;
}
