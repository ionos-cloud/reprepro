/*  This file is part of "reprepro"
 *  Copyright (C) 2009 Bernhard R. Link
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
#include <limits.h>
#include <stdint.h>
#include <string.h>
#ifdef TEST_GLOBMATCH
#include <stdio.h>
#include <stdlib.h>
#endif
#include "error.h"
#include "globmatch.h"

#ifdef NOPARANOIA
#define Assert(a) /* */
#else
#define Assert(a) assert(a)
#endif

/* check if a string matches a pattern, the pattern may
   contain * and ?.

   This algorithm should be in O( strlen(pattern) * strlen(string) )
*/

bool globmatch(const char *string, const char *pattern) {
	int i, l = strlen(pattern);
	int smallest_possible = 0, largest_possible = 0;
	bool possible[ l + 1 ];
	const char *p;

	if (strlen(pattern) > (size_t)INT_MAX)
		return false;

	memset(possible, 0, sizeof(possible));
	/* the first character must match the first pattern character
	   or the first one after the first star */
	possible[smallest_possible] = true;
	while (pattern[largest_possible] == '*')
		largest_possible++;
	Assert (largest_possible <= l);
	possible[largest_possible] = true;

	for (p = string ; *p != '\0' ; p++) {
		Assert (largest_possible >= smallest_possible);
		for (i = largest_possible ; i >= smallest_possible ; i--) {
			if (!possible[i])
				continue;
			/* no character matches the end of the pattern: */
			if (pattern[i] == '\0') {
				Assert (i == l);
				possible[i] = false;
				do {
					if (largest_possible <=
							smallest_possible)
						return false;
					largest_possible--;
				} while (!possible[largest_possible]);
				i = largest_possible + 1;
				continue;
			}
			Assert (i < l);
			if (pattern[i] == '*') {
				int j = i + 1;

				while (pattern[j] == '*')
					j++;
				/* all the '*' match one character: */
				Assert (j <= l);
				possible[j] = true;
				if (j > largest_possible)
					largest_possible = j;
				/* or more than one */
				continue;
			}
			if (pattern[i] == '[') {
				int j = i+1;
				bool matches = false, negate = false;

				if (pattern[j] == '!' || pattern[j] == '^') {
					j++;
					negate = true;
				}
				if (pattern[j] == '\0')
					return false;
				do {
					if (pattern[j+1] == '-' &&
						       pattern[j+2] != ']' &&
						       pattern[j+2] != '\0') {
						if (*p >= pattern[j] &&
						    *p <= pattern[j+2])
							matches = true;
						j += 3;
					} else {
						if (*p == pattern[j])
							matches = true;
						j++;
					}
					if (pattern[j] == '\0') {
						/* stray [ matches nothing */
						return false;
					}
				} while (pattern[j] != ']');
				j++;
				Assert (j <= l);
				if (negate)
					matches = !matches;
				if (matches) {
					possible[j] = true;
					/* if the next character is a star,
					   that might also match 0 characters */
					while (pattern[j] == '*')
						j++;
					Assert (j <= l);
					possible[j] = true;
					if (j > largest_possible)
						largest_possible = j;
				}
			} else if (pattern[i] == '?' || pattern[i] == *p) {
				int j = i + 1;
				possible[j] = true;
				/* if the next character is a star,
				   that might also match 0 characters */
				while (pattern[j] == '*')
					j++;
				Assert (j <= l);
				possible[j] = true;
				if (j > largest_possible)
					largest_possible = j;
			}
			possible[i] = false;
			if (i == smallest_possible) {
				smallest_possible++;
				while (!possible[smallest_possible]) {
					if (smallest_possible >=
							largest_possible)
						return false;
					smallest_possible++;
				}
				Assert (smallest_possible <= l);
			}
			if (i == largest_possible) {
				do {
					if (largest_possible <=
							smallest_possible)
						return false;
					largest_possible--;
				} while (!possible[largest_possible]);
				Assert (largest_possible >= 0);
			}
		}
	}
	/* end of string matches end of pattern,
	   if largest got < smallest, then this is also false */
	return possible[l];
}

#ifdef TEST_GLOBMATCH
int main(int argc, const char *argv[]) {
	if (argc != 3) {
		fputs("Wrong number of arguments!\n", stderr);
		exit(EXIT_FAILURE);
	}
	if (globmatch(argv[2], argv[1])) {
		puts("true");
		return 0;
	} else {
		puts("false");
		return 0;
	}
}
#endif
