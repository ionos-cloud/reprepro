/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/stat.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <archive.h>
#include <archive_entry.h>
#include "error.h"
#include "ar.h"
#include "chunks.h"
#include "filelist.h"
#include "debfile.h"

#ifndef HAVE_LIBARCHIVE
#error Why did this file got compiled?
#endif

static retvalue read_data_tar(/*@out@*/char **list, /*@out@*/size_t *size, const char *debfile, struct ar_archive *ar, struct archive *tar) {
	struct archive_entry *entry;
	struct filelistcompressor c;
	retvalue r;
	int a;

	r = filelistcompressor_setup(&c);
	if( RET_WAS_ERROR(r) )
		return r;

	archive_read_support_format_tar(tar);
	a = archive_read_open(tar,ar,
			ar_archivemember_open,
			ar_archivemember_read,
			ar_archivemember_close);
	if( a != ARCHIVE_OK ) {
		filelistcompressor_cancel(&c);
		fprintf(stderr,"open data.tar.gz within '%s' failed: %d:%d:%s\n",
				debfile,
				a,archive_errno(tar),
				archive_error_string(tar));
		return RET_ERROR;
	}
	while( (a=archive_read_next_header(tar, &entry)) == ARCHIVE_OK ) {
		const char *name = archive_entry_pathname(entry);
		mode_t mode;

		if( name[0] == '.' )
			name++;
		if( name[0] == '/' )
			name++;
		if( name[0] == '\0' )
			continue;
		mode = archive_entry_mode(entry);
		if( !S_ISDIR(mode) ) {
			r = filelistcompressor_add(&c, name, strlen(name));
			if( RET_WAS_ERROR(r) ) {
				filelistcompressor_cancel(&c);
				return r;
			}
		}
		if( interrupted() ) {
			filelistcompressor_cancel(&c);
			return RET_ERROR_INTERRUPTED;
		}
		a = archive_read_data_skip(tar);
		if( a != ARCHIVE_OK ) {
			int e = archive_errno(tar);
			printf("Error skipping %s within data.tar.gz from %s: %d=%s\n",
					archive_entry_pathname(entry),
					debfile,
					e, archive_error_string(tar));
			filelistcompressor_cancel(&c);
			return (e!=0)?(RET_ERRNO(e)):RET_ERROR;
		}
	}
	if( a != ARCHIVE_EOF ) {
		int e = archive_errno(tar);
		printf("Error reading data.tar.gz from %s: %d=%s\n",
				debfile,
				e, archive_error_string(tar));
		filelistcompressor_cancel(&c);
		return (e!=0)?(RET_ERRNO(e)):RET_ERROR;
	}
	return filelistcompressor_finish(&c, list, size);
}


retvalue getfilelist(/*@out@*/char **filelist, size_t *size, const char *debfile) {
	struct ar_archive *ar;
	retvalue r;

	r = ar_open(&ar,debfile);
	if( RET_WAS_ERROR(r) )
		return r;
	assert( r != RET_NOTHING);
	do {
		char *filename;

		r = ar_nextmember(ar, &filename);
		if( RET_IS_OK(r) ) {
			if( strcmp(filename,"data.tar.gz") == 0 ) {
				struct archive *tar;

				tar = archive_read_new();
				archive_read_support_compression_gzip(tar);
				r = read_data_tar(filelist, size,
						debfile, ar, tar);
				archive_read_finish(tar);
				if( r != RET_NOTHING ) {
					ar_close(ar);
					free(filename);
					return r;
				}

			}
/* TODO: here some better heuristic would be nice,
 * but when compiling against the static libarchive this is what needed,
 * and when libarchive is compiled locally it will have bz2 support
 * essentially when we have it, too.
 * Thus this ifdef is quite a good choice, especially as no offical
 * files should have this member, anyway */
#ifdef HAVE_LIBBZ2
			if( strcmp(filename,"data.tar.bz2") == 0 ) {
				struct archive *tar;

				tar = archive_read_new();
				archive_read_support_compression_gzip(tar);
				archive_read_support_compression_bzip2(tar);
				r = read_data_tar(filelist, size,
						debfile, ar, tar);
				archive_read_finish(tar);
				if( r != RET_NOTHING ) {
					ar_close(ar);
					free(filename);
					return r;
				}

			}
#endif
			free(filename);
		}
	} while( RET_IS_OK(r) );
	ar_close(ar);
	fprintf(stderr,"Could not find a data.tar.gz file within '%s'!\n",debfile);
	return RET_ERROR_MISSING;
}
