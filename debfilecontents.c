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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <archive.h>
#include <archive_entry.h>
#include "error.h"
#include "uncompression.h"
#include "ar.h"
#include "filelist.h"
#include "debfile.h"

#ifndef HAVE_LIBARCHIVE
#error Why did this file got compiled?
#endif
#if ARCHIVE_VERSION_NUMBER < 3000000
#define archive_read_free archive_read_finish
#endif

static retvalue read_data_tar(/*@out@*/char **list, /*@out@*/size_t *size, const char *debfile, struct ar_archive *ar, struct archive *tar) {
	struct archive_entry *entry;
	struct filelistcompressor c;
	retvalue r;
	int a, e;

	r = filelistcompressor_setup(&c);
	if (RET_WAS_ERROR(r))
		return r;

	archive_read_support_format_tar(tar);
	archive_read_support_format_gnutar(tar);
	a = archive_read_open(tar, ar,
			ar_archivemember_open,
			ar_archivemember_read,
			ar_archivemember_close);
	if (a != ARCHIVE_OK) {
		filelistcompressor_cancel(&c);
		e = archive_errno(tar);
		if (e == -EINVAL) /* special code to say there is none */
			fprintf(stderr,
"open data.tar within '%s' failed: %s\n",
				debfile, archive_error_string(tar));
		else
			fprintf(stderr,
"open data.tar within '%s' failed: %d:%d:%s\n", debfile, a, e,
				archive_error_string(tar));
		return RET_ERROR;
	}
	while ((a=archive_read_next_header(tar, &entry)) == ARCHIVE_OK) {
		const char *name = archive_entry_pathname(entry);
		mode_t mode;

		if (name[0] == '.')
			name++;
		if (name[0] == '/')
			name++;
		if (name[0] == '\0')
			continue;
		mode = archive_entry_mode(entry);
		if (!S_ISDIR(mode)) {
			r = filelistcompressor_add(&c, name, strlen(name));
			if (RET_WAS_ERROR(r)) {
				filelistcompressor_cancel(&c);
				return r;
			}
		}
		if (interrupted()) {
			filelistcompressor_cancel(&c);
			return RET_ERROR_INTERRUPTED;
		}
		a = archive_read_data_skip(tar);
		if (a != ARCHIVE_OK) {
			e = archive_errno(tar);
			if (e == -EINVAL) {
				r = RET_ERROR;
				fprintf(stderr,
"Error skipping %s within data.tar from %s: %s\n",
					archive_entry_pathname(entry),
					debfile, archive_error_string(tar));
			} else {
				fprintf(stderr,
"Error %d skipping %s within data.tar from %s: %s\n",
					e, archive_entry_pathname(entry),
					debfile, archive_error_string(tar));
				if (e != 0)
					r = RET_ERRNO(e);
				else
					r = RET_ERROR;
			}
			filelistcompressor_cancel(&c);
			return r;
		}
	}
	if (a != ARCHIVE_EOF) {
		e = archive_errno(tar);
		if (e == -EINVAL) {
			r = RET_ERROR;
			fprintf(stderr,
"Error reading data.tar from %s: %s\n", debfile, archive_error_string(tar));
		} else {
			fprintf(stderr,
"Error %d reading data.tar from %s: %s\n",
					e, debfile, archive_error_string(tar));
			if (e != 0)
				r = RET_ERRNO(e);
			else
				r = RET_ERROR;
		}
		filelistcompressor_cancel(&c);
		return r;
	}
	return filelistcompressor_finish(&c, list, size);
}


retvalue getfilelist(/*@out@*/char **filelist, size_t *size, const char *debfile) {
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
			if (strncmp(filename, "data.tar", 8) != 0) {
				free(filename);
				continue;
			}
			hadcandidate = true;
			for (c = 0 ; c < c_COUNT ; c++) {
				if (strcmp(filename + 8,
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
				int a;

				tar = archive_read_new();
				r = read_data_tar(filelist, size,
						debfile, ar, tar);
				a = archive_read_close(tar);
				if (a != ARCHIVE_OK && !RET_WAS_ERROR(r)) {
					int e = archive_errno(tar);
					if (e == -EINVAL)
						fprintf(stderr,
"reading data.tar within '%s' failed: %s\n",
							debfile,
							archive_error_string(
								tar));
					else
						fprintf(stderr,
"reading data.tar within '%s' failed: %d:%d:%s\n", debfile, a, e,
							archive_error_string(
								tar));
					r = RET_ERROR;
				}
				a = archive_read_free(tar);
				if (a != ARCHIVE_OK && !RET_WAS_ERROR(r)) {
					r = RET_ERROR;
				}
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
"Could not find a suitable data.tar file within '%s'!\n", debfile);
	else
		fprintf(stderr,
"Could not find a data.tar file within '%s'!\n", debfile);
	return RET_ERROR_MISSING;
}
