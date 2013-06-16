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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <archive.h>
#include <archive_entry.h>
#include "error.h"
#include "uncompression.h"
#include "ar.h"
#include "chunks.h"
#include "debfile.h"

#ifndef HAVE_LIBARCHIVE
#error Why did this file got compiled instead of extractcontrol.c?
#endif
#if ARCHIVE_VERSION_NUMBER < 3000000
#define archive_read_free archive_read_finish
#endif

static retvalue read_control_file(char **control, const char *debfile, struct archive *tar, struct archive_entry *entry) {
	int64_t size;
	char *buffer, *n;
	const char *afterchanges;
	size_t len, controllen;
	ssize_t got;

	size = archive_entry_size(entry);
	if (size <= 0) {
		fprintf(stderr, "Error: Empty control file within %s!\n",
				debfile);
		return RET_ERROR;
	}
	if (size > 10*1024*1024) {
		fprintf(stderr,
"Error: Ridiculously long control file within %s!\n",
				debfile);
		return RET_ERROR;
	}
	buffer = malloc(size + 2);
	if (FAILEDTOALLOC(buffer))
		return RET_ERROR_OOM;
	len = 0;
	while ((got = archive_read_data(tar, buffer+len, ((size_t)size+1)-len)) > 0
			&& !interrupted()) {
		len += got;
		if (len > (size_t)size) {
			fprintf(stderr,
"Internal Error: libarchive miscalculated length of the control file inside '%s',\n"
" perhaps the file is corrupt, perhaps libarchive!\n", debfile);
			free(buffer);
			return RET_ERROR;
		}
	}
	if (interrupted()) {
		free(buffer);
		return RET_ERROR_INTERRUPTED;
	}
	if (got < 0) {
		free(buffer);
		fprintf(stderr, "Error reading control file from %s\n",
				debfile);
		return RET_ERROR;
	}
	if (len < (size_t)size)
		fprintf(stderr,
"Warning: libarchive miscalculated length of the control file inside '%s'.\n"
"Maybe the file is corrupt, perhaps libarchive!\n", debfile);
	buffer[len] = '\0';

	controllen = chunk_extract(buffer, buffer, len, true, &afterchanges);

	if (controllen == 0) {
		fprintf(stderr,
"Could only find spaces within control file of '%s'!\n",
				debfile);
		free(buffer);
		return RET_ERROR;
	}
	if ((size_t)(afterchanges - buffer) < len) {
		if (*afterchanges == '\0')
			fprintf(stderr,
"Unexpected \\0 character within control file of '%s'!\n", debfile);
		else
			fprintf(stderr,
"Unexpected data after ending empty line in control file of '%s'!\n", debfile);
		free(buffer);
		return RET_ERROR;
	}
	assert (buffer[controllen] == '\0');
	n = realloc(buffer, controllen+1);
	if (FAILEDTOALLOC(n)) {
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
	archive_read_support_format_gnutar(tar);
	a = archive_read_open(tar, ar,
			ar_archivemember_open,
			ar_archivemember_read,
			ar_archivemember_close);
	if (a != ARCHIVE_OK) {
		fprintf(stderr,
"open control.tar.gz within '%s' failed: %d:%d:%s\n",
				debfile,
				a, archive_errno(tar),
				archive_error_string(tar));
		return RET_ERROR;
	}
	while ((a=archive_read_next_header(tar, &entry)) == ARCHIVE_OK) {
		if (strcmp(archive_entry_pathname(entry), "./control") != 0 &&
		    strcmp(archive_entry_pathname(entry), "control") != 0) {
			a = archive_read_data_skip(tar);
			if (a != ARCHIVE_OK) {
				int e = archive_errno(tar);
				printf(
"Error skipping %s within data.tar.gz from %s: %d=%s\n",
						archive_entry_pathname(entry),
						debfile,
						e, archive_error_string(tar));
				return (e!=0)?(RET_ERRNO(e)):RET_ERROR;
			}
			if (interrupted())
				return RET_ERROR_INTERRUPTED;
		} else {
			r = read_control_file(control, debfile, tar, entry);
			if (r != RET_NOTHING)
				return r;
		}
	}
	if (a != ARCHIVE_EOF) {
		int e = archive_errno(tar);
		printf("Error reading control.tar.gz from %s: %d=%s\n",
				debfile,
				e, archive_error_string(tar));
		return (e!=0)?(RET_ERRNO(e)):RET_ERROR;
	}
	fprintf(stderr,
"Could not find a control file within control.tar.gz within '%s'!\n",
			debfile);
	return RET_ERROR_MISSING;
}

retvalue extractcontrol(char **control, const char *debfile) {
	struct ar_archive *ar;
	retvalue r;
	bool hadcandidate = false;

	r = ar_open(&ar, debfile);
	if (RET_WAS_ERROR(r))
		return r;
	assert (r != RET_NOTHING);
	do {
		char *filename;
		enum compression c;

		r = ar_nextmember(ar, &filename);
		if (RET_IS_OK(r)) {
			if (strncmp(filename, "control.tar", 11) != 0) {
				free(filename);
				continue;
			}
			hadcandidate = true;
			for (c = 0 ; c < c_COUNT ; c++) {
				if (strcmp(filename + 11,
						uncompression_suffix[c]) == 0)
					break;
			}
			if (c >= c_COUNT) {
				free(filename);
				continue;
			}
			ar_archivemember_setcompression(ar, c);
			if (uncompression_supported(c)) {
				struct archive *tar;

				tar = archive_read_new();
				r = read_control_tar(control, debfile, ar, tar);
				// TODO run archive_read_close to get error messages?
				archive_read_free(tar);
				if (r != RET_NOTHING) {
					ar_close(ar);
					free(filename);
					return r;
				}

			}
			free(filename);
		}
	} while (RET_IS_OK(r));
	ar_close(ar);
	if (hadcandidate)
		fprintf(stderr,
"Could not find a suitable control.tar file within '%s'!\n", debfile);
	else
		fprintf(stderr,
"Could not find a control.tar file within '%s'!\n", debfile);
	return RET_ERROR_MISSING;
}
