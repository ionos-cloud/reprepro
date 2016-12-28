/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007 Bernhard R. Link
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

#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include "error.h"
#include "ignore.h"
#include "strlist.h"
#include "mprintf.h"
#include "names.h"
#include "checks.h"

typedef unsigned char uchar;

/* check if the character starting where <character> points
 * at is a overlong one */
static inline bool overlongUTF8(const char *character) {
	/* This checks for overlong utf-8 characters.
	 * (as they might mask '.' '\0' or '/' chars).
	 * we assume no filesystem/ar/gpg code will parse
	 * invalid utf8, as we would only be able to rule
	 * this out if we knew it is utf8 we are coping
	 * with. (Well, you should not use --ignore=validchars
	 * anyway). */
	uchar c = *character;

	if ((c & (uchar)0xC2 /*11000010*/) == (uchar)0xC0 /*11000000*/) {
		uchar nextc = *(character+1);

		if ((nextc & (uchar)0xC0 /*11000000*/)
				!= (uchar)0x80 /*10000000*/)
			return false;

		if ((c & (uchar)0x3E /* 00111110 */) == (uchar)0)
			return true;
		if (c == (uchar)0xE0 /*11100000*/ &&
		    (nextc & (uchar)0x20 /*00100000*/) == (uchar)0)
			return true;
		if (c == (uchar)0xF0 /*11110000*/ &&
		    (nextc & (uchar)0x30 /*00110000*/) == (uchar)0)
			return true;
		if (c == (uchar)0xF8 /*11111000*/ &&
		    (nextc & (uchar)0x38 /*00111000*/) == (uchar)0)
			return true;
		if (c == (uchar)0xFC /*11111100*/ &&
		    (nextc & (uchar)0x3C /*00111100*/) == (uchar)0)
			return true;
	}
	return false;
}

#define REJECTLOWCHARS(s, str, descr) \
	if ((uchar)*s < (uchar)' ') { \
		fprintf(stderr, \
			"Character 0x%02hhx not allowed within %s '%s'!\n", \
			*s, descr, str); \
		return RET_ERROR; \
	}

#define REJECTCHARIF(c, s, str, descr) \
	if (c) { \
		fprintf(stderr, \
			"Character '%c' not allowed within %s '%s'!\n", \
			*s, descr, string); \
		return RET_ERROR; \
	}


/* check if this is something that can be used as directory safely */
retvalue propersourcename(const char *string) {
	const char *s;
	bool firstcharacter = true;

	if (string[0] == '\0') {
		/* This is not really ignoreable, as this will lead
		 * to paths not normalized, so all checks go wrong */
		fprintf(stderr, "Source name is not allowed to be empty!\n");
		return RET_ERROR;
	}
	if (string[0] == '.') {
		/* A dot is not only hard to see, it would cause the directory
		 * to become /./.bla, which is quite dangerous. */
		fprintf(stderr,
"Source names are not allowed to start with a dot!\n");
		return RET_ERROR;
	}
	s = string;
	while (*s != '\0') {
		if ((*s > 'z' || *s < 'a') &&
		    (*s > '9' || *s < '0') &&
		    (firstcharacter ||
		     (*s != '+' && *s != '-' && *s != '.'))) {
			REJECTLOWCHARS(s, string, "sourcename");
			REJECTCHARIF (*s == '/', s, string, "sourcename");
			if (overlongUTF8(s)) {
				fprintf(stderr,
"This could contain an overlong UTF8 sequence, rejecting source name '%s'!\n",
					string);
				return RET_ERROR;
			}
			if (!IGNORING_(forbiddenchar,
"Character 0x%02hhx not allowed in sourcename: '%s'!\n", *s, string)) {
				return RET_ERROR;
			}
			if (ISSET(*s, 0x80)) {
				if (!IGNORING_(8bit,
"8bit character in source name: '%s'!\n", string)) {
					return RET_ERROR;
				}
			}
		}
		s++;
		firstcharacter = false;
	}
	return RET_OK;
}

/* check if this is something that can be used as directory safely */
retvalue properfilename(const char *string) {
	const char *s;

	if (string[0] == '\0') {
		fprintf(stderr, "Error: empty filename!\n");
		return RET_ERROR;
	}
	if ((string[0] == '.' && string[1] == '\0') ||
		(string[0] == '.' && string[1] == '.' && string[2] == '\0')) {
		fprintf(stderr, "File name not allowed: '%s'!\n", string);
		return RET_ERROR;
	}
	for (s = string ; *s != '\0'  ; s++) {
		REJECTLOWCHARS(s, string, "filename");
		REJECTCHARIF (*s == '/' , s, string, "filename");
		if (ISSET(*s, 0x80)) {
			if (overlongUTF8(s)) {
				fprintf(stderr,
"This could contain an overlong UTF8 sequence, rejecting file name '%s'!\n",
						string);
				return RET_ERROR;
			}
			if (!IGNORING_(8bit,
"8bit character in file name: '%s'!\n",	string)) {
				return RET_ERROR;
			}
		}
	}
	return RET_OK;
}

static const char *formaterror(const char *format, ...) {
	va_list ap;
	static char *data = NULL;

	if (data != NULL)
		free(data);
	va_start(ap, format);
	data = vmprintf(format, ap);
	va_end(ap);
	if (data == NULL)
		return "Out of memory";
	return data;
}

/* check if this is something that can be used as directory *and* identifier safely */
const char *checkfordirectoryandidentifier(const char *string) {
	const char *s;

	assert (string != NULL && string[0] != '\0');

	if ((string[0] == '.' && (string[1] == '\0'||string[1]=='/')))
		return "'.' is not allowed as directory part";
	if ((string[0] == '.' && string[1] == '.'
				&& (string[2] == '\0'||string[2] =='/')))
		return "'..' is not allowed as directory part";
	for (s = string; *s != '\0'; s++) {
		if (*s == '|')
			return "'|' is not allowed";
		if ((uchar)*s < (uchar)' ')
			return formaterror("Character 0x%02hhx not allowed", *s);
		if (*s == '/' && s[1] == '.' && (s[2] == '\0' || s[2] == '/'))
			return "'.' is not allowed as directory part";
		if (*s == '/' && s[1] == '.' && s[2] == '.'
				&& (s[3] == '\0' || s[3] =='/'))
			return "'..' is not allowed as directory part";
		if (*s == '/' && s[1] == '/')
			return "\"//\" is not allowed";
		if (ISSET(*s, 0x80)) {
			if (overlongUTF8(s))
				return
"Contains overlong UTF-8 sequence if treated as UTF-8";
			if (!IGNORABLE(8bit))
				return
"Contains 8bit character (use --ignore=8bit to ignore)";
		}
	}
	return NULL;
}

/* check if this can be used as part of identifier (and as part of a filename) */
const char *checkforidentifierpart(const char *string) {
	const char *s;

	assert (string != NULL && string[0] != '\0');

	for (s = string; *s != '\0' ; s++) {
		if (*s == '|')
			return "'|' is not allowed";
		if (*s == '/')
			return "'/' is not allowed";
		if ((uchar)*s < (uchar)' ')
			return formaterror("Character 0x%02hhx not allowed", *s);
		if (ISSET(*s, 0x80)) {
			if (overlongUTF8(s))
				return
"Contains overlong UTF-8 sequence if treated as UTF-8";
			if (!IGNORABLE(8bit))
				return
"Contains 8bit character (use --ignore=8bit to ignore)";
		}
	}
	return NULL;
}

retvalue properfilenamepart(const char *string) {
	const char *s;

	for (s = string ; *s != '\0' ; s++) {
		REJECTLOWCHARS(s, string, "filenamepart");
		REJECTCHARIF (*s == '/' , s, string, "filenamepart");
		if (ISSET(*s, 0x80)) {
			if (overlongUTF8(s)) {
				fprintf(stderr,
"This could contain an overlong UTF8 sequence, rejecting part of file name '%s'!\n",
					string);
				return RET_ERROR;
			}
			if (!IGNORING_(8bit,
"8bit character in part of file name: '%s'!\n",
					string))
				return RET_ERROR;
		}
	}
	return RET_OK;
}

retvalue properversion(const char *string) {
	const char *s = string;
	bool hadepoch = false;
	bool first = true;
	bool yetonlydigits = true;

	if (string[0] == '\0' && !IGNORING(emptyfilenamepart,
"A version string is empty!\n")) {
		return RET_ERROR;
	}
	if ((*s < '0' || *s > '9') &&
	    ((*s >= 'a' && *s <= 'z') || (*s >='A' && *s <= 'Z'))) {
		/* As there are official packages violating the rule
		 * of policy 5.6.11 to start with a digit, disabling
		 * this test, and only omitting a warning. */
		if (verbose >= 0)
			fprintf(stderr,
"Warning: Package version '%s' does not start with a digit, violating 'should'-directive in policy 5.6.11\n",
				string);
	}
	for (; *s != '\0' ; s++, first=false) {
		if ((*s <= '9' && *s >= '0')) {
			continue;
		}
		if (!first && yetonlydigits && *s == ':') {
			hadepoch = true;
			continue;
		}
		yetonlydigits = false;
		if ((*s >= 'A' && *s <= 'Z') ||
		           (*s >= 'a' && *s <= 'z')) {
			yetonlydigits = false;
			continue;
		}
		if (first || (*s != '+'  && *s != '-'
					&& *s != '.'  && *s != '~'
					&& (!hadepoch || *s != ':'))) {
			REJECTLOWCHARS(s, string, "version");
			REJECTCHARIF (*s == '/' , s, string, "version");
			if (overlongUTF8(s)) {
				fprintf(stderr,
"This could contain an overlong UTF8 sequence, rejecting version '%s'!\n",
						string);
				return RET_ERROR;
			}
			if (!IGNORING_(forbiddenchar,
"Character '%c' not allowed in version: '%s'!\n", *s, string))
				return RET_ERROR;
			if (ISSET(*s, 0x80)) {
				if (!IGNORING_(8bit,
"8bit character in version: '%s'!\n", string))
					return RET_ERROR;
			}
		}
	}
	return RET_OK;
}

retvalue properfilenames(const struct strlist *names) {
	int i;

	for (i = 0 ; i < names->count ; i ++) {
		retvalue r = properfilename(names->values[i]);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

retvalue properpackagename(const char *string) {
	const char *s;
	bool firstcharacter = true;

	/* To be able to avoid multiple warnings,
	 * this should always be a subset of propersourcename */

	if (string[0] == '\0') {
		/* This is not really ignoreable, as this is a primary
		 * key for our database */
		fprintf(stderr, "Package name is not allowed to be empty!\n");
		return RET_ERROR;
	}
	s = string;
	while (*s != '\0') {
		/* DAK also allowed upper case letters last I looked, policy
		 * does not, so they are not allowed without --ignore=forbiddenchar */
		// perhaps some extra ignore-rule for upper case?
		if ((*s > 'z' || *s < 'a') &&
		    (*s > '9' || *s < '0') &&
		    (firstcharacter
		     || (*s != '+' && *s != '-' && *s != '.'))) {
			REJECTLOWCHARS(s, string, "package name");
			REJECTCHARIF (*s == '/' , s, string, "package name");
			if (overlongUTF8(s)) {
				fprintf(stderr,
"This could contain an overlong UTF8 sequence, rejecting package name '%s'!\n",
						string);
				return RET_ERROR;
			}
			if (!IGNORING(forbiddenchar,
"Character 0x%02hhx not allowed in package name: '%s'!\n", *s, string)) {
				return RET_ERROR;
			}
			if (ISSET(*s, 0x80)) {
				if (!IGNORING_(8bit,
"8bit character in package name: '%s'!\n", string)) {
					return RET_ERROR;
				}
			}
		}
		s++;
		firstcharacter = false;
	}
	return RET_OK;
}

