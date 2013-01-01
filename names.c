/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2008 Bernhard R. Link
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
#include <sys/types.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"

char *calc_addsuffix(const char *str1, const char *str2) {
	return mprintf("%s.%s", str1, str2);
}

char *calc_dirconcat(const char *str1, const char *str2) {
	return mprintf("%s/%s", str1, str2);
}

char *calc_dirconcat3(const char *str1, const char *str2, const char *str3) {
	return mprintf("%s/%s/%s", str1, str2, str3);
}

/* Create a strlist consisting out of calc_dirconcat'ed entries of the old */
retvalue calc_dirconcats(const char *directory, const struct strlist *basefilenames,
						struct strlist *files) {
	retvalue r;
	int i;

	assert (directory != NULL && basefilenames != NULL && files != NULL);

	r = strlist_init_n(basefilenames->count, files);
	if (RET_WAS_ERROR(r))
		return r;

	r = RET_NOTHING;
	for (i = 0 ; i < basefilenames->count ; i++) {
		char *file;

		file = calc_dirconcat(directory, basefilenames->values[i]);
		if (FAILEDTOALLOC(file)) {
			strlist_done(files);
			return RET_ERROR_OOM;
		}
		r = strlist_add(files, file);
		if (RET_WAS_ERROR(r)) {
			strlist_done(files);
			return r;
		}
	}
	return r;

}

retvalue calc_inplacedirconcats(const char *directory, struct strlist *files) {
	int i;

	assert (directory != NULL && files != NULL );
	for (i = 0 ; i < files->count ; i++) {
		char *file;

		file = calc_dirconcat(directory, files->values[i]);
		if (FAILEDTOALLOC(file))
			return RET_ERROR_OOM;
		free(files->values[i]);
		files->values[i] = file;
	}
	return RET_OK;
}

void names_overversion(const char **version, bool epochsuppressed) {
	const char *n = *version;
	bool hadepoch = epochsuppressed;

	if (*n < '0' || *n > '9') {
		if ((*n < 'a' || *n > 'z') && (*n < 'A' || *n > 'Z'))
			return;
	} else
		n++;
	while (*n >= '0' && *n <= '9')
		n++;
	if (*n == ':') {
		hadepoch = true;
		n++;
	}
	while ((*n >= '0' && *n <= '9') || (*n >= 'a' && *n <= 'z')
			|| (*n >= 'A' && *n <= 'Z') || *n == '.' || *n == '~'
			|| *n == '-' || *n == '+' || (hadepoch && *n == ':'))
		n++;
	*version = n;
}

char *calc_trackreferee(const char *codename, const char *sourcename, const char *sourceversion) {
	return	mprintf("%s %s %s", codename, sourcename, sourceversion);
}

char *calc_changes_basename(const char *name, const char *version, const struct strlist *architectures) {
	size_t name_l, version_l, l;
	int i;
	char *n, *p;

	name_l = strlen(name);
	version_l = strlen(version);
	l = name_l + version_l + sizeof("__.changes");

	for (i = 0 ; i < architectures->count ; i++) {
		l += strlen(architectures->values[i]);
		if (i != 0)
			l++;
	}
	n = malloc(l);
	if (FAILEDTOALLOC(n))
		return n;
	p = n;
	memcpy(p, name, name_l); p+=name_l;
	*(p++) = '_';
	memcpy(p, version, version_l); p+=version_l;
	*(p++) = '_';
	for (i = 0 ; i < architectures->count ; i++) {
		size_t a_l = strlen(architectures->values[i]);
		if (i != 0)
			*(p++) = '+';
		assert ((size_t)((p+a_l)-n) < l);
		memcpy(p, architectures->values[i], a_l);
		p += a_l;
	}
	assert ((size_t)(p-n) < l-8);
	memcpy(p, ".changes", 9); p += 9;
	assert (*(p-1) == '\0');
	assert ((size_t)(p-n) == l);
	return n;
}
