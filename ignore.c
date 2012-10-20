/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
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
#include <string.h>
#include <stdlib.h>

#include "ignore.h"

int ignored[IGN_COUNT];
bool ignore[IGN_COUNT];
enum config_option_owner owner_ignore[IGN_COUNT];

static const char * const ignores[] = {
#define IGN(what) #what ,
	VALID_IGNORES
#undef IGN
};

bool print_ignore_type_message(bool i, enum ignore what) {
	ignored[what]++;
	if (ignore[what])
		fprintf(stderr, "%s as --ignore=%s given.\n",
				i ? "Ignoring" : "Not rejecting",
				ignores[what]);
	else
		fprintf(stderr, "To ignore use --ignore=%s.\n",
				ignores[what]);
	return ignore[what];
}

static retvalue set(const char *given, size_t len, bool newvalue, enum config_option_owner newowner) {
	int i;

	//TODO: allow multiple values sperated by some sign here...

	for (i = 0 ; i < IGN_COUNT ; i++) {
		if (strncmp(given, ignores[i], len) == 0 &&
				ignores[i][len] == '\0') {
			if (owner_ignore[i] <= newowner) {
				ignore[i] = newvalue;
				owner_ignore[i] = newowner;
			}
			break;
		}
	}
	if (i == IGN_COUNT) {
		char *str = strndup(given, len);
		if (IGNORING(ignore,
"Unknown --ignore value: '%s'!\n", (str!=NULL)?str:given)) {
			free(str);
			return RET_NOTHING;
		} else {
			free(str);
			return RET_ERROR;
		}
	} else
		return RET_OK;
}

retvalue set_ignore(const char *given, bool newvalue, enum config_option_owner newowner) {
	const char *g, *p;
	retvalue r;

	assert (given != NULL);

	g = given;

	while (true) {
		p = g;
		while (*p != '\0' && *p != ',')
			p++;
		if (p == g) {
			fprintf(stderr,
"Empty ignore option in --ignore='%s'!\n",
					given);
			return RET_ERROR_MISSING;
		}
		r = set(g, p - g, newvalue, newowner);
		if (RET_WAS_ERROR(r))
			return r;
		if (*p == '\0')
			return RET_OK;
		g = p+1;
	}
}
