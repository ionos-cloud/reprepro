/*  This file is part of "reprepro"
 *  Copyright (C) 2006 Bernhard R. Link
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
#include "md5sum.h"
#include "chunks.h"
#include "debfile.h"

#ifndef HAVE_LIBARCHIVE
#error Why did this file got compiled instead of extractcontrol.c?
#endif

static retvalue read_control_file(char **control, const char *debfile, struct archive *tar, struct archive_entry *entry) {
	int64_t size;
	char *buffer, *startofchanges, *endofchanges, *afterchanges, *n;
	size_t len;
	ssize_t got;

	size = archive_entry_size(entry);
	if( size <= 0 ) {
		fprintf(stderr, "Error: Empty control file within %s!\n", debfile);
		return RET_ERROR;
	}
	if( size > 10*1024*1024 ) {
		fprintf(stderr, "Error: Ridiculous long control file within %s!\n", debfile);
		return RET_ERROR;
	}
	buffer = malloc(size+2);
	len = 0;
	while( (got = archive_read_data(tar, buffer+len, ((size_t)size+1)-len)) > 0
			&& !interrupted() ) {
		len += got;
		if( len > size ) {
			fprintf(stderr, "Internal Error: libarchive miscalculated length of the control file within '%s',\n"
			" perhaps the file is corrupt, perhaps libarchive!\n", debfile);
			free(buffer);
			return RET_ERROR;
		}
	}
	if( interrupted() ) {
		free(buffer);
		return RET_ERROR_INTERRUPTED;
	}
	if( got < 0 ) {
		free(buffer);
		fprintf(stderr, "Error reading control file from %s\n", debfile);
		return RET_ERROR;
	}
	if( len < size )
		fprintf(stderr, "Warning: libarchive overcalculated length of the control file within '%s',\n"
			" perhaps the file is corrupt, perhaps libarchive!\n", debfile);
	buffer[len] = '\0';

	startofchanges = buffer;
	while( *startofchanges != '\0' && xisspace(*startofchanges)) {
		startofchanges++;
	}
	if( (size_t)(startofchanges-buffer) >= len) {
		fprintf(stderr,"Could only find spaces within contol file of '%s'!\n",
				debfile);
		free(buffer);
		return RET_ERROR;
	}
	len -= (startofchanges-buffer);
	memmove(buffer, startofchanges, len+1);
	endofchanges = buffer;
	while( *endofchanges != '\0' &&
		( *endofchanges != '\n' || *(endofchanges-1)!= '\n')) {
		endofchanges++;
	}
	afterchanges = endofchanges;
	while(  *afterchanges != '\0' && xisspace(*afterchanges)) {
		afterchanges++;
	}
	if( (size_t)(afterchanges - buffer) < len ) {
		if( *afterchanges == '\0' ) {
			fprintf(stderr,"Unexpected \\0 character within control file of '%s'!\n", debfile);
			free(buffer);
			return RET_ERROR;
		}
		fprintf(stderr,"Unexpected data after ending empty line in control file of '%s'!\n", debfile);
		free(buffer);
		return RET_ERROR;
	}
	*afterchanges = '\0';
	n = realloc(buffer, (afterchanges-buffer)+1);
	if( n == NULL ) {
		free(buffer);
		return RET_ERROR_OOM;
	}
	*control = n;
	return RET_OK;
}

static retvalue read_control_tar(char **control, const char *debfile, struct ar_archive *ar, struct archive *tar) {
	struct archive_entry *entry;
	int a;
	retvalue r;

	archive_read_support_format_tar(tar);
	a = archive_read_open(tar,ar,
			ar_archivemember_open,
			ar_archivemember_read,
			ar_archivemember_close);
	if( a != ARCHIVE_OK ) {
		fprintf(stderr,"open control.tar.gz within '%s' failed: %d:%d:%s\n",
				debfile,
				a,archive_errno(tar),
				archive_error_string(tar));
		return RET_ERROR;
	}
	while( (a=archive_read_next_header(tar, &entry)) == ARCHIVE_OK ) {
		if( strcmp(archive_entry_pathname(entry),"./control") != 0 &&
		    strcmp(archive_entry_pathname(entry),"control") != 0 ) {
			a = archive_read_data_skip(tar);
			if( a != ARCHIVE_OK ) {
				int e = archive_errno(tar);
				printf("Error skipping %s within data.tar.gz from %s: %d=%s\n",
						archive_entry_pathname(entry),
						debfile,
						e, archive_error_string(tar));
				return (e!=0)?(RET_ERRNO(e)):RET_ERROR;
			}
			if( interrupted() )
				return RET_ERROR_INTERRUPTED;
		} else {
			r = read_control_file(control, debfile, tar, entry);
			if( r != RET_NOTHING )
				return r;
		}
	}
	if( a != ARCHIVE_EOF ) {
		int e = archive_errno(tar);
		printf("Error reading control.tar.gz from %s: %d=%s\n",
				debfile,
				e, archive_error_string(tar));
		return (e!=0)?(RET_ERRNO(e)):RET_ERROR;
	}
	fprintf(stderr,"Could not find a control file within control.tar.gz within '%s'!\n",debfile);
	return RET_ERROR_MISSING;
}

retvalue extractcontrol(char **control,const char *debfile) {
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
			if( strcmp(filename,"control.tar.gz") == 0 ) {
				struct archive *tar;

				tar = archive_read_new();
				archive_read_support_compression_gzip(tar);
				r = read_control_tar(control, debfile, ar, tar);
				archive_read_finish(tar);
				if( r != RET_NOTHING ) {
					ar_close(ar);
					free(filename);
					return r;
				}

			}
/* see below for a discussion about this #ifdef */
#ifdef HAVE_LIBBZ2
			if( strcmp(filename,"control.tar.bz2") == 0 ) {
				struct archive *tar;

				tar = archive_read_new();
				archive_read_support_compression_gzip(tar);
				archive_read_support_compression_bzip2(tar);
				r = read_control_tar(control, debfile, ar, tar);
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
	fprintf(stderr,"Could not find a control.tar.gz file within '%s'!\n",debfile);
	return RET_ERROR_MISSING;
}
