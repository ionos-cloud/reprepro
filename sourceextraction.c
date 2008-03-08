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
#include <string.h>
#include "error.h"
#include "readcompressed.h"
#include "sourceextraction.h"

struct sourceextraction {
	bool failed, completed;
	int difffile, tarfile;
	enum compression diffcompression, tarcompression;
	/*@null@*/ char **section_p, **priority_p;
};

struct sourceextraction *sourceextraction_init(char **section_p, char **priority_p) {
	struct sourceextraction *n;

	n = calloc(1, sizeof(struct sourceextraction));
	if( FAILEDTOALLOC(n) )
		return n;
	n->difffile = -1;
	n->tarfile = -1;
	n->section_p = section_p;
	n->priority_p = priority_p;
	return n;
}

void sourceextraction_abort(struct sourceextraction *e) {
	free(e);
}

/* with must be a string constant, no pointer! */
#define endswith(name, len, with) (len >= sizeof(with) && memcmp(name+(len+1-sizeof(with)), with, sizeof(with)-1) == 0 )

/* register a file part of this source */
void sourceextraction_setpart(struct sourceextraction *e, int i, const char *basename) {
	size_t bl = strlen(basename);
	enum compression c;

	if( e->failed )
		return;

	if( endswith(basename, bl, ".gz" ) ) {
		c = c_gzipped;
		bl -= 3;
	} else if( endswith(basename, bl, ".bz2" ) ) {
		c = c_bzipped;
		bl -= 4;
	} else if( endswith(basename, bl, ".lzma" ) ) {
		c = c_bzipped;
		bl -= 5;
	} else {
		c = c_uncompressed;
	}
	if( endswith(basename, bl, ".dsc" ) )
		return;
	else if( endswith(basename, bl, ".diff" ) ) {
		e->difffile = i;
		e->diffcompression = c;
		return;
	} else if( endswith(basename, bl, ".tar" ) ) {
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
	if( e->failed || e->completed )
		return false;
	if( e->difffile >= 0 ) {
		if( unsupportedcompression(e->diffcompression) )
			// TODO: errormessage
			return false;
		*ofs_p = e->difffile;
		return true;
	} else if( e->tarfile >= 0 ) {
#ifdef HAVE_LIBARCHIVE
		if( unsupportedcompression(e->tarcompression) )
			// TODO: errormessage
			return false;
		*ofs_p = e->tarfile;
		return true;
#else
		return false;
#endif
	} else
		return false;
}

static retvalue parsediff(struct readcompressed *f, /*@null@*/char **section_p, /*@null@*/char **priority_p, bool *found_p) {
	size_t destlength, lines_in, lines_out;
	const char *p, *s;

	/* we are assuming the exact format dpkg-source generates here... */

	if( !readcompressed_getline(f, &p) ) {
		/* empty file */
		*found_p = false;
		return RET_OK;
	}
	if( unlikely(memcmp(p, "--- ", 4) != 0) )
		return RET_NOTHING;
	if( !readcompressed_getline(f, &p) )
		/* so short a file? */
		return RET_NOTHING;
	if( unlikely(memcmp(p, "+++ ", 4) != 0) )
		return RET_NOTHING;
	p += 4;
	s = strchr(p, '/');
	if( unlikely(s == NULL) )
		return RET_NOTHING;
	destlength = s - p;
	/* ignore all files that are not x/debian/control */
	while( strcmp(s, "debian/control") != 0 ) {
		if( !readcompressed_getline(f, &p) )
			return RET_NOTHING;
		while( memcmp(p, "@@ -", 4) == 0) {
			p += 4;
			while( *p != ',' ) {
				if( unlikely(*p == '\0') )
					return RET_NOTHING;
				p++;
			}
			p++;
			lines_in = 0;
			while( *p >= '0' && *p <= '9' ) {
				lines_in = 10*lines_in + (*p-'0');
				p++;
			}
			while( *p == ' ' )
				p++;
			if( unlikely(*(p++) != '+') )
				return RET_NOTHING;
			while( *p >= '0' && *p <= '9' )
				p++;
			if( *p == ',' ) {
				p++;
				lines_out = 0;
				while( *p >= '0' && *p <= '9' ) {
					lines_out = 10*lines_out + (*p-'0');
					p++;
				}
			} else
				lines_out = 1;
			while( *p == ' ' )
				p++;
			if( unlikely(*p != '@') )
				return RET_NOTHING;

			while( lines_in > 0 || lines_out > 0 ) {
				char ch;

				ch = readcompressed_overlinegetchar(f);
				switch( ch ) {
					case '+':
						if( unlikely(lines_out == 0) )
							return RET_NOTHING;
						lines_out--;
						break;
					case ' ':
						if( unlikely(lines_out == 0) )
							return RET_NOTHING;
						lines_out--;
					case '-':
						if( unlikely(lines_in == 0) )
							return RET_NOTHING;
						lines_in--;
						break;
					default:
						return RET_NOTHING;
				}
			}
			if( !readcompressed_getline(f, &p) ) {
				*found_p = false;
				/* nothing found successfully */
				return RET_OK;
			}
		}
		if( unlikely(memcmp(p, "--- ", 4) != 0) )
			return RET_NOTHING;
		if( !readcompressed_getline(f, &p) )
			return RET_NOTHING;
		if( unlikely(memcmp(p, "+++ ", 4) != 0) )
			return RET_NOTHING;
		s = p + 4 + destlength;
		if( unlikely(*s != '/') )
			return RET_NOTHING;
		s++;
	}
	/* found debian/control */
	if( !readcompressed_getline(f, &p) )
		return RET_NOTHING;
	if( unlikely(memcmp(p, "@@ -", 4) != 0) )
		return RET_NOTHING;
	p += 4;
	p++;
	while( *p != ',' ) {
		if( unlikely(*p == '\0') )
			return RET_NOTHING;
		p++;
	}
	p++;
	while( *p >= '0' && *p <= '9' )
		p++;
	while( *p == ' ' )
		p++;
	if( unlikely(*(p++) != '+') )
		return RET_NOTHING;
	if( *(p++) != '1' || *(p++) != ',' ) {
		/* a diff not starting at the first line is not yet supported */
		return RET_NOTHING;
	}
	lines_out = 0;
	while( *p >= '0' && *p <= '9' ) {
		lines_out = 10*lines_out + (*p-'0');
		p++;
	}
	while( *p == ' ' )
		p++;
	if( unlikely(*p != '@') )
		return RET_NOTHING;
	while( lines_out > 0 ) {
		if( !readcompressed_getline(f, &p) )
			return RET_NOTHING;

		switch( *(p++) ) {
			case '-':
				break;
			default:
				return RET_NOTHING;
			case ' ':
			case '+':
				if( unlikely(lines_out == 0) )
					return RET_NOTHING;
				lines_out--;
				if( section_p != NULL &&
				    strncasecmp(p, "Section:", 8) == 0 ) {
					p += 8;
					while( *p == ' ' || *p == '\t' )
						p++;
					s = p;
					while( *s != ' ' && *s != '\t' &&
					       *s != '\0' && *s != '\r' )
						s++;
					if( s == p )
						return RET_NOTHING;
					*section_p = strndup(p, s-p);
					if( FAILEDTOALLOC(*section_p) )
						return RET_ERROR_OOM;
					while( *s == ' ' || *s == '\t' ||
					       *s == '\r' )
						s++;
					if( *s != '\0' )
						return RET_NOTHING;
					continue;
				}
				if( priority_p != NULL &&
				    strncasecmp(p, "Priority:", 9) == 0 ) {
					p += 9;
					while( *p == ' ' || *p == '\t' )
						p++;
					s = p;
					while( *s != ' ' && *s != '\t' &&
					       *s != '\0' && *s != '\r' )
						s++;
					if( s == p )
						return RET_NOTHING;
					*priority_p = strndup(p, s-p);
					if( FAILEDTOALLOC(*priority_p) )
						return RET_ERROR_OOM;
					while( *s == ' ' || *s == '\t' ||
					       *s == '\r' )
						s++;
					if( *s != '\0' )
						return RET_NOTHING;
					continue;
				}
				if( *p == '\0' ) {
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

/* full file name of requested files ready to analyse */
retvalue sourceextraction_analyse(struct sourceextraction *e, const char *fullfilename) {
	retvalue r;
	bool found IFSTUPIDCC(= false);

#ifndef HAVE_LIBARCHIVE
	assert( e->difffile >= 0 );
#endif
	if( e->difffile >= 0 ) {
		struct readcompressed *f;

		assert( !unsupportedcompression(e->diffcompression) );
		e->difffile = -1;

		r = readcompressed_open(&f, fullfilename, e->diffcompression);
		if( !RET_IS_OK(r) ) {
			e->failed = true;
			return r;
		}
		r = parsediff(f, e->section_p, e->priority_p, &found);
		if( RET_IS_OK(r) )
			r = readcompressed_close(f);
		else
			readcompressed_abort(f);
		if( !RET_IS_OK(r) )
			e->failed = true;
		else if( found )
			/* do not look in the tar, we found debian/control */
			e->completed = true;
		return r;
	}

	/* if it's not the diff, look into the .tar file */
	assert( e->tarfile >= 0 );
	assert( !unsupportedcompression(e->tarcompression) );
	e->tarfile = -1;

	// TODO: implement this. This is a bit more complicated because those
	// are native upstream tars and dpkg-source has even special code to
	// handle all kind of broken tars.
	return RET_NOTHING;
}

retvalue sourceextraction_finish(struct sourceextraction *e) {
	if( e->completed ) {
		free(e);
		return RET_OK;
	}
	free(e);
	return RET_NOTHING;
}
