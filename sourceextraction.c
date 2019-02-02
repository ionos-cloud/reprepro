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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#if ARCHIVE_VERSION_NUMBER < 3000000
#define archive_read_free archive_read_finish
#endif
#endif

#include "error.h"
#include "filecntl.h"
#include "chunks.h"
#include "uncompression.h"
#include "sourceextraction.h"

struct sourceextraction {
	bool failed, completed;
	int difffile, tarfile, debiantarfile;
	enum compression diffcompression, tarcompression, debiancompression;
	/*@null@*/ char **section_p, **priority_p;
};

struct sourceextraction *sourceextraction_init(char **section_p, char **priority_p) {
	struct sourceextraction *n;

	n = zNEW(struct sourceextraction);
	if (FAILEDTOALLOC(n))
		return n;
	n->difffile = -1;
	n->tarfile = -1;
	n->debiantarfile = -1;
	n->section_p = section_p;
	n->priority_p = priority_p;
	return n;
}

void sourceextraction_abort(struct sourceextraction *e) {
	free(e);
}

/* with must be a string constant, no pointer! */
#define endswith(name, len, with) (len >= sizeof(with) && memcmp(name+(len+1-sizeof(with)), with, sizeof(with)-1) == 0)

/* register a file part of this source */
void sourceextraction_setpart(struct sourceextraction *e, int i, const char *basefilename) {
	size_t bl = strlen(basefilename);
	enum compression c;

	if (e->failed)
		return;

	c = compression_by_suffix(basefilename, &bl);

	if (endswith(basefilename, bl, ".dsc"))
		return;
	else if (endswith(basefilename, bl, ".asc"))
		return;
	else if (endswith(basefilename, bl, ".diff")) {
		e->difffile = i;
		e->diffcompression = c;
		return;
	} else if (endswith(basefilename, bl, ".debian.tar")) {
		e->debiantarfile = i;
		e->debiancompression = c;
		return;
	} else if (endswith(basefilename, bl, ".tar")) {
		e->tarfile = i;
		e->tarcompression = c;
		return;
	} else {
		// TODO: errormessage
		e->failed = true;
	}
}

/* return the next needed file */
bool sourceextraction_needs(struct sourceextraction *e, int *ofs_p) {
	if (e->failed || e->completed)
		return false;
	if (e->difffile >= 0) {
		if (!uncompression_supported(e->diffcompression))
			// TODO: errormessage
			return false;
		*ofs_p = e->difffile;
		return true;
	} else if (e->debiantarfile >= 0) {
#ifdef HAVE_LIBARCHIVE
		if (!uncompression_supported(e->debiancompression))
			return false;
		*ofs_p = e->debiantarfile;
		return true;
#else
		return false;
#endif
	} else if (e->tarfile >= 0) {
#ifdef HAVE_LIBARCHIVE
		if (!uncompression_supported(e->tarcompression))
			return false;
		*ofs_p = e->tarfile;
		return true;
#else
		return false;
#endif
	} else
		return false;
}

static retvalue parsediff(struct compressedfile *f, /*@null@*/char **section_p, /*@null@*/char **priority_p, bool *found_p) {
	size_t destlength, lines_in, lines_out;
	const char *p, *s; char *garbage;
#define BUFSIZE 4096
	char buffer[BUFSIZE];
	int bytes_read, used = 0, filled = 0;

	auto inline bool u_getline(void);
	inline bool u_getline(void) {
		do {
		if (filled - used > 0) {
			char *n;

			p = buffer + used;
			n = memchr(p, '\n', filled - used);
			if (n != NULL) {
				used += 1 + (n - p);
				*n = '\0';
				while (--n >= p && *n == '\r')
					*n = '\0';
				return true;
			}
		} else { assert (filled == used);
			filled = 0;
			used = 0;
		}
		if (filled == BUFSIZE) {
			if (used == 0)
				/* overlong line */
				return false;
			memmove(buffer, buffer + used, filled - used);
			filled -= used;
			used = 0;
		}
		bytes_read = uncompress_read(f, buffer + filled,
				BUFSIZE - filled);
		if (bytes_read <= 0)
			return false;
		filled += bytes_read;
		} while (true);
	}
	auto inline char u_overlinegetchar(void);
	inline char u_overlinegetchar(void) {
		const char *n;
		char ch;

		if (filled - used > 0) {
			ch = buffer[used];
		} else { assert (filled == used);
			used = 0;
			bytes_read = uncompress_read(f, buffer, BUFSIZE);
			if (bytes_read <= 0) {
				filled = 0;
				return '\0';
			}
			filled = bytes_read;
			ch = buffer[0];
		}
		if (ch == '\n')
			return '\0';

		/* over rest of the line */
		n = memchr(buffer + used, '\n', filled - used);
		if (n != NULL) {
			used = 1 + (n - buffer);
			return ch;
		}
		used = 0;
		filled = 0;
		/* need to read more to get to the end of the line */
		do { /* these lines can be long */
			bytes_read = uncompress_read(f, buffer, BUFSIZE);
			if (bytes_read <= 0)
				return false;
			n = memchr(buffer, '\n', bytes_read);
		} while (n == NULL);
		used = 1 + (n - buffer);
		filled = bytes_read;
		return ch;
	}

	/* we are assuming the exact format dpkg-source generates here... */

	if (!u_getline()) {
		/* empty or strange file */
		*found_p = false;
		return RET_OK;
	}
	if (memcmp(p, "diff ", 4) == 0) {
		/* one exception is allowing diff lines,
		 * as diff -ru adds them ... */
		if (!u_getline()) {
			/* strange file */
			*found_p = false;
			return RET_OK;
		}
	}
	if (unlikely(memcmp(p, "--- ", 4) != 0))
		return RET_NOTHING;
	if (!u_getline())
		/* so short a file? */
		return RET_NOTHING;
	if (unlikely(memcmp(p, "+++ ", 4) != 0))
		return RET_NOTHING;
	p += 4;
	s = strchr(p, '/');
	if (unlikely(s == NULL))
		return RET_NOTHING;
	s++;
	/* another exception to allow diff output directly:
	 * +++ lines might have garbage after a tab... */
	garbage = strchr(s, '\t');
	if (garbage != NULL)
		*garbage = '\0';
	destlength = s - p;
	/* ignore all files that are not x/debian/control */
	while (strcmp(s, "debian/control") != 0) {
		if (unlikely(interrupted()))
			return RET_ERROR_INTERRUPTED;
		if (!u_getline())
			return RET_NOTHING;
		while (memcmp(p, "@@ -", 4) == 0) {
			if (unlikely(interrupted()))
				return RET_ERROR_INTERRUPTED;
			p += 4;
			while (*p != ',' && *p != ' ') {
				if (unlikely(*p == '\0'))
					return RET_NOTHING;
				p++;
			}
			if (*p == ' ')
				lines_in = 1;
			else {
				p++;
				lines_in = 0;
				while (*p >= '0' && *p <= '9') {
					lines_in = 10*lines_in + (*p-'0');
					p++;
				}
			}
			while (*p == ' ')
				p++;
			if (unlikely(*(p++) != '+'))
				return RET_NOTHING;
			while (*p >= '0' && *p <= '9')
				p++;
			if (*p == ',') {
				p++;
				lines_out = 0;
				while (*p >= '0' && *p <= '9') {
					lines_out = 10*lines_out + (*p-'0');
					p++;
				}
			} else if (*p == ' ')
				lines_out = 1;
			else
				return RET_NOTHING;
			while (*p == ' ')
				p++;
			if (unlikely(*p != '@'))
				return RET_NOTHING;

			while (lines_in > 0 || lines_out > 0) {
				char ch;

				ch = u_overlinegetchar();
				switch (ch) {
					case '+':
						if (unlikely(lines_out == 0))
							return RET_NOTHING;
						lines_out--;
						break;
					case ' ':
						if (unlikely(lines_out == 0))
							return RET_NOTHING;
						lines_out--;
						/* no break */
						__attribute__ ((fallthrough));
					case '-':
						if (unlikely(lines_in == 0))
							return RET_NOTHING;
						lines_in--;
						break;
					default:
						return RET_NOTHING;
				}
			}
			if (!u_getline()) {
				*found_p = false;
				/* nothing found successfully */
				return RET_OK;
			}
		}
		if (memcmp(p, "\\ No newline at end of file", 27) == 0) {
			if (!u_getline()) {
				/* nothing found successfully */
				*found_p = false;
				return RET_OK;
			}
		}
		if (memcmp(p, "diff ", 4) == 0) {
			if (!u_getline()) {
				/* strange file, but nothing explicitly wrong */
				*found_p = false;
				return RET_OK;
			}
		}
		if (unlikely(memcmp(p, "--- ", 4) != 0))
			return RET_NOTHING;
		if (!u_getline())
			return RET_NOTHING;
		if (unlikely(memcmp(p, "+++ ", 4) != 0))
			return RET_NOTHING;
		p += 4;
		s = strchr(p, '/');
		if (unlikely(s == NULL))
			return RET_NOTHING;
		/* another exception to allow diff output directly:
		 * +++ lines might have garbage after a tab... */
		garbage = strchr(s, '\t');
		if (garbage != NULL)
			*garbage = '\0';
		/* if it does not always have the same directory, then
		 * we cannot be sure it has no debian/control, so we
		 * have to fail... */
		s++;
		if (s != p + destlength)
			return RET_NOTHING;
	}
	/* found debian/control */
	if (!u_getline())
		return RET_NOTHING;
	if (unlikely(memcmp(p, "@@ -", 4) != 0))
		return RET_NOTHING;
	p += 4;
	p++;
	while (*p != ',' && *p != ' ') {
		if (unlikely(*p == '\0'))
			return RET_NOTHING;
		p++;
	}
	if (*p == ',') {
		p++;
		while (*p >= '0' && *p <= '9')
			p++;
	}
	while (*p == ' ')
		p++;
	if (unlikely(*(p++) != '+'))
		return RET_NOTHING;
	if (*(p++) != '1' || *(p++) != ',') {
		/* a diff not starting at the first line (or not being
		 * more than one line) is not yet supported */
		return RET_NOTHING;
	}
	lines_out = 0;
	while (*p >= '0' && *p <= '9') {
		lines_out = 10*lines_out + (*p-'0');
		p++;
	}
	while (*p == ' ')
		p++;
	if (unlikely(*p != '@'))
		return RET_NOTHING;
	while (lines_out > 0) {
		if (unlikely(interrupted()))
			return RET_ERROR_INTERRUPTED;
		if (!u_getline())
			return RET_NOTHING;

		switch (*(p++)) {
			case '-':
				break;
			default:
				return RET_NOTHING;
			case ' ':
			case '+':
				if (unlikely(lines_out == 0))
					return RET_NOTHING;
				lines_out--;
				if (section_p != NULL &&
				    strncasecmp(p, "Section:", 8) == 0) {
					p += 8;
					while (*p == ' ' || *p == '\t')
						p++;
					s = p;
					while (*s != ' ' && *s != '\t' &&
					       *s != '\0' && *s != '\r')
						s++;
					if (s == p)
						return RET_NOTHING;
					*section_p = strndup(p, s-p);
					if (FAILEDTOALLOC(*section_p))
						return RET_ERROR_OOM;
					while (*s == ' ' || *s == '\t' ||
					       *s == '\r')
						s++;
					if (*s != '\0')
						return RET_NOTHING;
					continue;
				}
				if (priority_p != NULL &&
				    strncasecmp(p, "Priority:", 9) == 0) {
					p += 9;
					while (*p == ' ' || *p == '\t')
						p++;
					s = p;
					while (*s != ' ' && *s != '\t' &&
					       *s != '\0' && *s != '\r')
						s++;
					if (s == p)
						return RET_NOTHING;
					*priority_p = strndup(p, s-p);
					if (FAILEDTOALLOC(*priority_p))
						return RET_ERROR_OOM;
					while (*s == ' ' || *s == '\t' ||
					       *s == '\r')
						s++;
					if (*s != '\0')
						return RET_NOTHING;
					continue;
				}
				if (*p == '\0') {
					/* end of control data, we are
					 * finished */
					*found_p = true;
					return RET_OK;
				}
				break;
		}
	}
	/* cannot yet handle a .diff not containing the full control */
	return RET_NOTHING;
}

#ifdef HAVE_LIBARCHIVE
static retvalue read_source_control_file(struct sourceextraction *e, struct archive *tar, struct archive_entry *entry) {
	// TODO: implement...
	size_t size, len, controllen;
	ssize_t got;
	char *buffer;
	const char *aftercontrol;

	size = archive_entry_size(entry);
	if (size <= 0)
		return RET_NOTHING;
	if (size > 10*1024*1024)
		return RET_NOTHING;
	buffer = malloc(size+2);
	if (FAILEDTOALLOC(buffer))
		return RET_ERROR_OOM;
	len = 0;
	while ((got = archive_read_data(tar, buffer+len, ((size_t)size+1)-len)) > 0
			&& !interrupted()) {
		len += got;
		if (len > size) {
			free(buffer);
			return RET_NOTHING;
		}
	}
	if (unlikely(interrupted())) {
		free(buffer);
		return RET_ERROR_INTERRUPTED;
	}
	if (got < 0) {
		free(buffer);
		return RET_NOTHING;
	}
	buffer[len] = '\0';
	// TODO: allow a saved .diff for this file applied here

	controllen = chunk_extract(buffer, buffer, len, true, &aftercontrol);
	if (controllen == 0) {
		free(buffer);
		return RET_NOTHING;
	}

	if (e->section_p != NULL)
		(void)chunk_getvalue(buffer, "Section", e->section_p);
	if (e->priority_p != NULL)
		(void)chunk_getvalue(buffer, "Priority", e->priority_p);
	free(buffer);
	return RET_OK;
}

static int compressedfile_open(UNUSED(struct archive *a), UNUSED(void *v)) {
	return ARCHIVE_OK;
}

static int compressedfile_close(UNUSED(struct archive *a), UNUSED(void *v)) {
	return ARCHIVE_OK;
}

static ssize_t compressedfile_read(UNUSED(struct archive *a), void *d, const void **buffer_p) {
	struct compressedfile *f = d;
	// TODO malloc buffer instead
	static char mybuffer[4096];

	*buffer_p = mybuffer;
	return uncompress_read(f, mybuffer, 4096);
}

static retvalue parse_tarfile(struct sourceextraction *e, const char *filename, enum compression c, /*@out@*/bool *found_p) {
	struct archive *tar;
	struct archive_entry *entry;
	struct compressedfile *file;
	int a;
	retvalue r, r2;

	/* While an .tar, especially an .orig.tar can be very ugly
	 * (they should be pristine upstream tars, so dpkg-source works around
	 * a lot of ugliness),
	 * we are looking for debian/control. This is unlikely to be in an ugly
	 * upstream tar verbatimly. */

	if (!isregularfile(filename))
		return RET_NOTHING;

	tar = archive_read_new();
	if (FAILEDTOALLOC(tar))
		return RET_ERROR_OOM;
	archive_read_support_format_tar(tar);
	archive_read_support_format_gnutar(tar);

	r = uncompress_open(&file, filename, c);
	if (!RET_IS_OK(r)) {
		archive_read_free(tar);
		return r;
	}

	a = archive_read_open(tar, file, compressedfile_open,
			compressedfile_read, compressedfile_close);
	if (a != ARCHIVE_OK) {
		int err = archive_errno(tar);
		if (err != -EINVAL && err != 0)
			fprintf(stderr,
"Error %d trying to extract control information from %s:\n" "%s\n",
				err, filename, archive_error_string(tar));
		else
			fprintf(stderr,
"Error trying to extract control information from %s:\n" "%s\n",
				filename, archive_error_string(tar));
		archive_read_free(tar);
		uncompress_abort(file);
		return RET_ERROR;
	}
	while ((a=archive_read_next_header(tar, &entry)) == ARCHIVE_OK) {
		const char *name = archive_entry_pathname(entry);
		const char *s;
		bool iscontrol;

		if (name[0] == '.' && name[1] == '/')
			name += 2;
		s = strchr(name, '/');
		if (s == NULL)
			// TODO: is this already enough to give up totally?
			iscontrol = false;
		else
			iscontrol = strcmp(s+1, "debian/control") == 0 ||
				    strcmp(name, "debian/control") == 0;

		if (iscontrol) {
			r = read_source_control_file(e, tar, entry);
			archive_read_free(tar);
			r2 = uncompress_error(file);
			RET_UPDATE(r, r2);
			uncompress_abort(file);
			*found_p = true;
			return r;
		}
		a = archive_read_data_skip(tar);
		if (a != ARCHIVE_OK) {
			int err = archive_errno(tar);
			printf("Error %d skipping %s within %s: %s\n",
					err, name, filename,
					archive_error_string(tar));
			archive_read_free(tar);
			if (err == 0 || err == -EINVAL)
				r = RET_ERROR;
			else
				r =  RET_ERRNO(err);
			r2 = uncompress_error(file);
			RET_UPDATE(r, r2);
			uncompress_abort(file);
			return r;
		}
		if (interrupted())
			return RET_ERROR_INTERRUPTED;
	}
	if (a != ARCHIVE_EOF) {
		int err = archive_errno(tar);
		fprintf(stderr, "Error %d reading %s: %s\n",
				err, filename, archive_error_string(tar));
		archive_read_free(tar);
		if (err == 0 || err == -EINVAL)
			r = RET_ERROR;
		else
			r =  RET_ERRNO(err);
		r2 = uncompress_error(file);
		RET_UPDATE(r, r2);
		uncompress_abort(file);
		return r;
	}
	archive_read_free(tar);
	*found_p = false;
	return uncompress_close(file);
}
#endif

/* full file name of requested files ready to analyse */
retvalue sourceextraction_analyse(struct sourceextraction *e, const char *fullfilename) {
	retvalue r;
	bool found;

#ifndef HAVE_LIBARCHIVE
	assert (e->difffile >= 0);
#endif
	if (e->difffile >= 0) {
		struct compressedfile *f;

		assert (uncompression_supported(e->diffcompression));
		e->difffile = -1;

		r = uncompress_open(&f, fullfilename, e->diffcompression);
		if (!RET_IS_OK(r)) {
			e->failed = true;
			/* being unable to read a file is no hard error... */
			return RET_NOTHING;
		}
		r = parsediff(f, e->section_p, e->priority_p, &found);
		if (RET_IS_OK(r))  {
			if (!found)
				r = uncompress_close(f);
			else {
				r = uncompress_error(f);
				uncompress_abort(f);
			}
		} else {
			uncompress_abort(f);
		}
		if (!RET_IS_OK(r))
			e->failed = true;
		else if (found)
			/* do not look in the tar, we found debian/control */
			e->completed = true;
		return r;
	}

#ifdef HAVE_LIBARCHIVE
	if (e->debiantarfile >= 0) {
		e->debiantarfile = -1;
		r = parse_tarfile(e, fullfilename, e->debiancompression,
				&found);
		if (!RET_IS_OK(r))
			e->failed = true;
		else if (found)
			/* do not look in the tar, we found debian/control */
			e->completed = true;
		return r;
	}
#endif

	/* if it's not the diff nor the .debian.tar, look into the .tar file: */
	assert (e->tarfile >= 0);
	e->tarfile = -1;

#ifdef HAVE_LIBARCHIVE
	r = parse_tarfile(e, fullfilename, e->tarcompression, &found);
	if (!RET_IS_OK(r))
		e->failed = true;
	else if (found)
		/* do not look in the tar, we found debian/control */
		e->completed = true;
	return r;
#else
	return RET_NOTHING;
#endif
}

retvalue sourceextraction_finish(struct sourceextraction *e) {
	if (e->completed) {
		free(e);
		return RET_OK;
	}
	free(e);
	return RET_NOTHING;
}
