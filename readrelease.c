/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007 Bernhard R. Link
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
#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "names.h"
#include "chunks.h"
#include "readtextfile.h"
#include "readrelease.h"

/* get a strlist with the md5sums of a Release-file */
retvalue release_getchecksums(const char *releasefile, const bool ignore[cs_hashCOUNT], struct checksumsarray *out) {
	retvalue r;
	char *chunk;
	struct strlist files[cs_hashCOUNT];
	enum checksumtype cs;

	r = readtextfile(releasefile, releasefile, &chunk, NULL);
	assert( r != RET_NOTHING );
	if( !RET_IS_OK(r) )
		return r;
	for( cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++ ) {
		if( ignore[cs] ) {
			strlist_init(&files[cs]);
			continue;
		}
		assert( release_checksum_names[cs] != NULL );
		r = chunk_getextralinelist(chunk, release_checksum_names[cs],
				&files[cs]);
		if( r == RET_NOTHING ) {
			if( cs == cs_md5sum ) {
				fprintf(stderr,
"Missing 'MD5Sum' field in Release file '%s'!\n",	releasefile);
				r = RET_ERROR;
			} else
				strlist_init(&files[cs]);
		}
		if( RET_WAS_ERROR(r) ) {
			while( cs-- > cs_md5sum ) {
				strlist_done(&files[cs]);
			}
			free(chunk);
			return r;
		}
	}
	free(chunk);

	r = checksumsarray_parse(out, files, releasefile);
	for( cs = cs_md5sum ; cs < cs_hashCOUNT ; cs++ ) {
		strlist_done(&files[cs]);
	}
	return r;
}
