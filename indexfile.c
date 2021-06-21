/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007,2008,2010,2016 Bernhard R. Link
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
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include "error.h"
#include "ignore.h"
#include "chunks.h"
#include "names.h"
#include "uncompression.h"
#include "package.h"
#include "indexfile.h"

/* the purpose of this code is to read index files, either from a snapshot
 * previously generated or downloaded while updating. */

struct indexfile {
	struct compressedfile *f;
	char *filename;
	int linenumber, startlinenumber;
	retvalue status;
	char *buffer;
	int size, ofs, content;
	bool failed;
};

retvalue indexfile_open(struct indexfile **file_p, const char *filename, enum compression compression) {
	struct indexfile *f = zNEW(struct indexfile);
	retvalue r;

	if (FAILEDTOALLOC(f))
		return RET_ERROR_OOM;
	f->filename = strdup(filename);
	if (FAILEDTOALLOC(f->filename)) {
		free(f);
		return RET_ERROR_OOM;
	}
	r = uncompress_open(&f->f, filename, compression);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		free(f->filename);
		free(f);
		return RET_ERRNO(errno);
	}
	f->linenumber = 0;
	f->startlinenumber = 0;
	f->status = RET_OK;
	f->size = 4*1024*1024;
	f->ofs = 0;
	f->content = 0;
	/* +1 for *d = '\0' in eof case */
	f->buffer = malloc(f->size + 1);
	if (FAILEDTOALLOC(f->buffer)) {
		uncompress_abort(f->f);
		free(f->filename);
		free(f);
		return RET_ERROR_OOM;
	}
	*file_p = f;
	return RET_OK;
}

retvalue indexfile_close(struct indexfile *f) {
	retvalue r;

	r = uncompress_close(f->f);

	free(f->filename);
	free(f->buffer);
	RET_UPDATE(r, f->status);
	free(f);

	return r;
}

static retvalue indexfile_get(struct indexfile *f) {
	char *p, *d, *e, *start;
	bool afternewline, nothingyet;
	int bytes_read;

	if (f->failed)
		return RET_ERROR;

	d = f->buffer;
	afternewline = true;
	nothingyet = true;
	do {
		start = f->buffer + f->ofs;
		p = start ;
		e = p + f->content;

		// TODO: if the chunk_get* are more tested with strange
		// input, this could be kept in-situ and only chunk_edit
		// beautifying this chunk...

		while (p < e) {
			/* just ignore '\r', even if not line-end... */
			if (*p == '\r') {
				p++;
				continue;
			}
			if (*p == '\n') {
				f->linenumber++;
				if (afternewline) {
					p++;
					f->content -= (p - start);
					f->ofs += (p - start);
					assert (f->ofs == (p - f->buffer));
					if (nothingyet)
						/* restart */
						return indexfile_get(f);
					if (d > f->buffer && *(d-1) == '\n')
						d--;
					*d = '\0';
					return RET_OK;
				}
				afternewline = true;
				nothingyet = false;
			} else
				afternewline = false;
			if (unlikely(*p == '\0')) {
				*(d++) = ' ';
				p++;
			} else
				*(d++) = *(p++);
		}
		/* ** out of data, read new ** */

		/* start at beginning of free space */
		f->ofs = (d - f->buffer);
		f->content = 0;

		if (f->size - f->ofs <= 2048) {
			/* Adding code to enlarge the buffer in this case
			 * is risky as hard to test properly.
			 *
			 * Also it is almost certainly caused by some
			 * mis-representation of the file or perhaps
			 * some attack. Requesting all existing memory in
			 * those cases does not sound very useful. */

			fprintf(stderr,
"Error parsing %s line %d: Ridiculous long (>= 256K) control chunk!\n",
					f->filename,
					f->startlinenumber);
			f->failed = true;
			return RET_ERROR;
		}

		bytes_read = uncompress_read(f->f, d, f->size - f->ofs);
		if (bytes_read < 0)
			return RET_ERROR;
		else if (bytes_read == 0)
			break;
		f->content = bytes_read;
	} while (true);

	if (d == f->buffer)
		return RET_NOTHING;

	/* end of file reached, return what we got so far */
	assert (f->content == 0);
	assert (d-f->buffer <= f->size);
	if (d > f->buffer && *(d-1) == '\n')
		d--;
	*d = '\0';
	return RET_OK;
}

bool indexfile_getnext(struct indexfile *f, struct package *pkgout, struct target *target, bool allowwrongarchitecture) {
	retvalue r;
	bool ignorecruft = false; // TODO
	char *packagename, *version;
	const char *control;
	architecture_t atom;

	packagename = NULL; version = NULL;
	do {
		free(packagename); packagename = NULL;
		free(version); version = NULL;
		f->startlinenumber = f->linenumber + 1;
		r = indexfile_get(f);
		if (!RET_IS_OK(r))
			break;
		control = f->buffer;
		r = chunk_getvalue(control, "Package", &packagename);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Error parsing %s line %d to %d: Chunk without 'Package:' field!\n",
					f->filename,
					f->startlinenumber, f->linenumber);
			if (!ignorecruft)
				r = RET_ERROR_MISSING;
			else
				continue;
		}
		if (RET_WAS_ERROR(r))
			break;

		r = chunk_getvalue(control, "Version", &version);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Error parsing %s line %d to %d: Chunk without 'Version:' field!\n",
					f->filename,
					f->startlinenumber, f->linenumber);
			if (!ignorecruft)
				r = RET_ERROR_MISSING;
			else
				continue;
		}
		if (RET_WAS_ERROR(r))
			break;
		if (target->packagetype == pt_dsc) {
			atom = architecture_source;
		} else {
			char *architecture;

			r = chunk_getvalue(control, "Architecture", &architecture);
			if (RET_WAS_ERROR(r))
				break;
			if (r == RET_NOTHING)
				architecture = NULL;

			/* check if architecture fits for target and error
			    out if not ignorewrongarchitecture */
			if (architecture == NULL) {
				fprintf(stderr,
"Error parsing %s line %d to %d: Chunk without 'Architecture:' field!\n",
						f->filename,
						f->startlinenumber, f->linenumber);
				if (!ignorecruft) {
					r = RET_ERROR_MISSING;
					break;
				} else
					continue;
			} else if (strcmp(architecture, "all") == 0) {
				atom = architecture_all;
			} else if (strcmp(architecture,
					   atoms_architectures[
						target->architecture
						]) == 0) {
				atom = target->architecture;
			} else if (!allowwrongarchitecture
					&& !ignore[IGN_wrongarchitecture]) {
				fprintf(stderr,
"Warning: ignoring package because of wrong 'Architecture:' field '%s'"
" (expected 'all' or '%s') in %s lines %d to %d!\n",
						architecture,
						atoms_architectures[
						target->architecture],
						f->filename,
						f->startlinenumber,
						f->linenumber);
				if (ignored[IGN_wrongarchitecture] == 0) {
					fprintf(stderr,
"This either mean the repository you get packages from is of an extremely\n"
"low quality, or something went wrong. Trying to ignore it now, though.\n"
"To no longer get this message use '--ignore=wrongarchitecture'.\n");
				}
				ignored[IGN_wrongarchitecture]++;
				free(architecture);
				continue;
			} else {
				/* just ignore this because of wrong
				 * architecture */
				free(architecture);
				continue;
			}
			free(architecture);
		}
		if (RET_WAS_ERROR(r))
			break;
		pkgout->target = target;
		pkgout->control = control;
		pkgout->pkgname = packagename;
		pkgout->name = pkgout->pkgname;
		pkgout->pkgversion = version;
		pkgout->version = pkgout->pkgversion;
		pkgout->architecture = atom;
		return true;
	} while (true);
	free(packagename);
	free(version);
	RET_UPDATE(f->status, r);
	return false;
}
