/*  This file is part of "reprepro"
 *  Copyright (C) 2009,2016 Bernhard R. Link
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
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "error.h"
#include "atoms.h"
#include "chunks.h"
#include "target.h"
#include "distribution.h"
#include "dirs.h"
#include "package.h"
#include "printlistformat.h"

retvalue listformat_print(const char *listformat, struct package *package) {
	struct target *target = package->target;
	retvalue r;
	const char *p, *q;

	if (listformat == NULL) {

		r = package_getversion(package);
		if (RET_IS_OK(r)) {
			printf( "%s: %s %s\n",
					target->identifier, package->name,
					package->version);
		} else {
			printf("Could not retrieve version from %s in %s\n",
					package->name, target->identifier);
		}
		return r;
	}
	/* try to produce the same output dpkg-query --show produces: */
	for (p = listformat ; *p != '\0' ; p++) {
		long length;
		char *value;
		const char *v;

		if (*p == '\\') {
			p++;
			if (*p == '\0')
				break;
			switch (*p) {
				case 'n':
					putchar('\n');
					break;
				case 't':
					putchar('\t');
					break;
				case 'r':
					putchar('\r');
					break;
				/* extension \0 produces zero byte
				 * (useful for xargs -0) */
				case '0':
					putchar('\0');
					break;
				default:
					putchar(*p);
			}
			continue;
		}
		if (*p != '$' || p[1] != '{') {
			putchar(*p);
			continue;
		}
		p++;
		/* substitute veriable */
		q = p;
		while (*q != '\0' && *q != '}' && *q != ';')
			q++;
		if (*q == '\0' || q == p) {
			putchar('$');
			putchar('{');
			continue;
		}
		if (q - p == 12 && strncasecmp(p, "{$identifier", 12) == 0) {
			value = NULL;
			v = target->identifier;
		} else if (   (q - p == 10 && strncasecmp(p, "{$basename", 10) == 0)
		           || (q - p == 14 && strncasecmp(p, "{$fullfilename", 14) == 0)
		           || (q - p ==  9 && strncasecmp(p, "{$filekey", 9) == 0)) {
			struct strlist filekeys;
			r = target->getfilekeys(package->control, &filekeys);
			if (RET_WAS_ERROR(r))
				return r;
			if (RET_IS_OK(r) && filekeys.count > 0) {
				if (q - p == 9) { /* filekey */
					value = filekeys.values[0];
					filekeys.values[0] = NULL;
					v = value;
				} else if (q - p == 10) { /* basename */
					value = filekeys.values[0];
					filekeys.values[0] = NULL;
					v = dirs_basename(value);;
				} else { /* fullfilename */
					value = calc_dirconcat(global.basedir,
							filekeys.values[0]);
					if (FAILEDTOALLOC(value))
						return RET_ERROR_OOM;
					v = value;
				}
				strlist_done(&filekeys);
			} else {
				value = NULL;
				v = "";
			}
		} else if (q - p == 6 && strncasecmp(p, "{$type", 6) == 0) {
			value = NULL;
			v = atoms_packagetypes[target->packagetype];
		} else if (q - p == 10 &&
				strncasecmp(p, "{$codename", 10) == 0) {
			value = NULL;
			v = target->distribution->codename;
		} else if (q - p == 14 &&
				strncasecmp(p, "{$architecture", 14) == 0) {
			value = NULL;
			v = atoms_architectures[target->architecture];
		} else if (q - p == 11 &&
				strncasecmp(p, "{$component", 11) == 0) {
			value = NULL;
			v = atoms_components[target->component];
		} else if (q - p == 8 && strncasecmp(p, "{$source", 8) == 0) {
			r = package_getsource(package);
			if (RET_WAS_ERROR(r))
				return r;
			if (RET_IS_OK(r)) {
				value = NULL;
				v = package->source;
			} else {
				value = NULL;
				v = "";
			}
		} else if (q - p == 15 && strncasecmp(p, "{$sourceversion", 15) == 0) {
			r = package_getsource(package);
			if (RET_WAS_ERROR(r))
				return r;
			if (RET_IS_OK(r)) {
				value = NULL;
				v = package->sourceversion;
			} else {
				value = NULL;
				v = "";
			}
		} else if (q - p == 8 && strncasecmp(p, "{package", 8) == 0) {
			value = NULL;
			v = package->name;
		} else {
			char *variable = strndup(p + 1, q - (p + 1));
			if (FAILEDTOALLOC(variable))
				return RET_ERROR_OOM;
			r = chunk_getwholedata(package->control,
					variable, &value);
			free(variable);
			if (RET_WAS_ERROR(r))
				return r;
			if (RET_IS_OK(r)) {
				v = value;
				while (*v != '\0' && xisspace(*v))
					v++;
			} else {
				value = NULL;
				v = "";
			}
		}
		if (*q == ';') {
			/* dpkg-query allows octal an hexadecimal,
			 * so we do, too */
			length = strtol(q + 1, (char**)&p, 0);
			if (*p != '}') {
				free(value);
				putchar('$');
				putchar('{');
				continue;
			}
		} else {
			p = q;
			length = 0;
		}
		/* as in dpkg-query, length 0 means unlimited */
		if (length == 0) {
			fputs(v, stdout);
		} else {
			long value_length = strlen(v);

			if (length < 0) {
				length = -length;
				while (value_length < length) {
					putchar(' ');
					length--;
				}
			}
			if (value_length > length) {
				fwrite(v, length, 1, stdout);
				length = 0;
			} else if (value_length > 0) {
				fwrite(v, value_length, 1, stdout);
				length -= value_length;
			}
			while (length-- > 0)
				putchar(' ');
		}
		free(value);
	}
	return RET_OK;
}

