/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007,2009 Bernhard R. Link
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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "globmatch.h"
#include "terms.h"

void term_free(term *t) {
	while (t != NULL) {
		struct term_atom *next = t->next;
		if (t->isspecial) {
			if (t->special.type != NULL &&
					t->special.type->done != NULL)
				t->special.type->done(t->comparison,
						&t->special.comparewith);

		} else {
			free(t->generic.key);
			free(t->generic.comparewith);
		}
		strlist_done(&t->architectures);
		free(t);
		t = next;
	}
}

static retvalue parseatom(const char **formula, /*@out@*/struct term_atom **atom, int options, const struct term_special *specials) {
	struct term_atom *a;
	const char *f = *formula;
#define overspace() while (*f != '\0' && xisspace(*f)) f++
	const char *keystart, *keyend;
	const char *valuestart, *valueend;
	enum term_comparison comparison = tc_none;
	bool negated = false;
	const struct term_special *s;


	overspace();
	if (*f == '!' && ISSET(options, T_NEGATION)) {
		negated = true;
		f++;
	}
	keystart = f;
	// TODO: allow more strict checking again with some option?
	while (*f != '\0' && *f != '(' && !xisspace(*f) && *f != ','
			&& *f != '|' && *f !='(' && *f != ')'
			&& *f != '[' && *f != '!')
		f++;
	keyend = f;
	if (keystart == keyend) {
		*formula = f;
		return RET_NOTHING;
	}
	overspace();
	if (ISSET(options, T_VERSION) && *f == '(') {
		f++;
		overspace();
		switch (*f) {
			case '>':
				f++;
				if (*f == '=') {
					comparison = tc_moreorequal;
					f++;
				} else if (*f == '>') {
					comparison = tc_strictmore;
					f++;
				} else {
					comparison = tc_moreorequal;
					fprintf(stderr,
"Warning: Found a '(>' without '=' or '>'  in '%s'(beginning cut), will be treated as '>='.\n",
							*formula);
				}
				break;
			case '<':
				f++;
				if (*f == '=') {
					comparison = tc_lessorequal;
					f++;
				} else if (*f == '<') {
					comparison = tc_strictless;
					f++;
				} else {
					comparison = tc_lessorequal;
					fprintf(stderr,
"Warning: Found a '(<' without '=' or '<'  in '%s'(begin cut), will be treated as '<='.\n",
							*formula);
				}
				break;
			case '=':
				f++;
				if (*f == '=')
					f++;
				else if (*f != ' ') {
					*formula = f;
					return RET_NOTHING;
				}
				comparison = tc_equal;
				break;
			case '%':
				if (ISSET(options, T_GLOBMATCH)) {
					f++;
					comparison = tc_globmatch;
					break;
				}
				*formula = f;
				return RET_NOTHING;
			case '!':
				if (f[1] == '%' &&
						ISSET(options, T_GLOBMATCH)) {
					f += 2;
					comparison = tc_notglobmatch;
					break;
				}
				if (ISSET(options, T_NOTEQUAL)) {
					f++;
					if (*f != '=') {
						*formula = f;
						return RET_NOTHING;
					}
					f++;
					comparison = tc_notequal;
					break;
				}
				*formula = f;
				return RET_NOTHING;
			default:
				*formula = f;
				return RET_NOTHING;
		}
		overspace();
		valueend = valuestart = f;
		while (*f != '\0' && *f != ')') {
			valueend = f+1;
			f++;
			while (*f != '\0' && xisspace(*f))
				f++;
		}
		if (*f != ')' || valueend == valuestart) {
			*formula = f;
			return RET_NOTHING;
		}
		f++;

	} else {
		comparison = tc_none;
		valuestart = valueend = NULL;
	}
	overspace();
	if (ISSET(options, T_ARCHITECTURES) && *f == '[') {
		//TODO: implement this one...
		assert ("Not yet implemented!" == NULL);
	}
	for (s = specials ; s->name != NULL ; s++) {
		if (strncasecmp(s->name, keystart, keyend-keystart) == 0 &&
				s->name[keyend-keystart] == '\0')
			break;
	}
	a = zNEW(struct term_atom);
	if (FAILEDTOALLOC(a))
		return RET_ERROR_OOM;
	a->negated = negated;
	a->comparison = comparison;
	if (s->name != NULL) {
		retvalue r;

		a->isspecial = true;
		a->special.type = s;
		r = s->parse(comparison, valuestart, valueend-valuestart,
				&a->special.comparewith);
		if (RET_WAS_ERROR(r)) {
			term_free(a);
			return r;
		}
	} else {
		a->isspecial = false;
		a->generic.key = strndup(keystart, keyend - keystart);
		if (FAILEDTOALLOC(a->generic.key)) {
			term_free(a);
			return RET_ERROR_OOM;
		}
		if (comparison != tc_none) {
			if (valueend - valuestart > 2048 &&
					(comparison == tc_globmatch ||
					 comparison == tc_notglobmatch)) {
				fprintf(stderr,
"Ridicilous long globmatch '%.10s...'!\n",
						valuestart);
				term_free(a);
				return RET_ERROR;
			}
			a->generic.comparewith = strndup(valuestart,
					valueend - valuestart);
			if (FAILEDTOALLOC(a->generic.comparewith)) {
				term_free(a);
				return RET_ERROR_OOM;
			}
		}
	}
	//TODO: here architectures, too

	*atom = a;
	*formula = f;
	return RET_OK;
#undef overspace
}

/* as this are quite special BDDs (a atom being false cannot make it true),
 * the places where True and False can be found are
 * quite easy and fast to find: */

static void orterm(term *termtochange, /*@dependent@*/term *termtoor) {
	struct term_atom *p = termtochange;

	while (p != NULL) {
		while (p->nextiffalse != NULL)
			p = p->nextiffalse;
		p->nextiffalse= termtoor;
		p = p->nextiftrue;
	}
}
static void andterm(term *termtochange, /*@dependent@*/term *termtoand) {
	struct term_atom *p = termtochange;

	while (p != NULL) {
		while (p->nextiftrue != NULL)
			p = p->nextiftrue;
		p->nextiftrue = termtoand;
		p = p->nextiffalse;
	}
}

retvalue term_compile(term **term_p, const char *origformula, int options, const struct term_special *specials) {
	const char *formula = origformula;
	/* for the global list */
	struct term_atom *first, *last;
	/* the atom just read */
	struct term_atom *atom;
	struct {
		/*@dependent@*/struct term_atom *firstinand, *firstinor;
	} levels[50];
	int lastinitializeddepth=-1;
	int depth=0;
	retvalue r;
	int i;
	//TODO: ???
	char junction = '\0';

	if (ISSET(options, T_ARCHITECTURES)) {
		//TODO: implement this one...
		assert ("Not yet implemented!" == NULL);
	}

#define overspace() while (*formula!='\0' && xisspace(*formula)) formula++

	lastinitializeddepth=-1;
	depth=0;
	first = last = NULL;

	while (true) {
		overspace();
		while (*formula == '(' && ISSET(options, T_BRACKETS)) {
			depth++; formula++;
			overspace();
		}
		if (depth >= 50) {
			term_free(first);
			fprintf(stderr,
"Nested too deep: '%s'!\n",
					origformula);
			return RET_ERROR;
		}
		r = parseatom(&formula, &atom, options, specials);
		if (r == RET_NOTHING) {
			if (*formula == '\0')
				fprintf(stderr,
"Unexpected end of string parsing formula '%s'!\n",
					origformula);
			else
				fprintf(stderr,
"Unexpected character '%c' parsing formula '%s'!\n",
					*formula, origformula);

			r = RET_ERROR;
		}
		if (RET_WAS_ERROR(r)) {
			term_free(first);
			return r;
		}
		for (i=lastinitializeddepth+1 ; i <= depth ; i ++) {
			levels[i].firstinand = atom;
			levels[i].firstinor = atom;
		}
		if (junction != '\0') {
			assert(lastinitializeddepth >= 0);
			assert (first != NULL);
			last->next = atom;
			last = atom;
			if (junction == ',') {
				andterm(levels[lastinitializeddepth].firstinand,
						atom);
				levels[lastinitializeddepth].firstinand = atom;
				levels[lastinitializeddepth].firstinor = atom;
			} else {
				assert (junction == '|');
				orterm(levels[lastinitializeddepth].firstinor,
						atom);
				levels[lastinitializeddepth].firstinor = atom;
			}
		} else {
			assert(lastinitializeddepth == -1);
			assert (first == NULL);
			first = last = atom;
		}
		lastinitializeddepth = depth;
		overspace();
		if (*formula == ')' && ISSET(options, T_BRACKETS)) {
			formula++;
			if (depth > 0) {
				depth--;
				lastinitializeddepth = depth;
			} else {
				fprintf(stderr,
"Too many ')'s in '%s'!\n",
						origformula);
				term_free(first);
				return RET_ERROR;
			}
			overspace();
		}
		overspace();
		if (*formula == '\0')
			break;
		if (*formula != ',' &&
				(*formula != '|' || NOTSET(options, T_OR))) {
			fprintf(stderr,
"Unexpected character '%c' within '%s'!\n",
				*formula, origformula);
			term_free(first);
			return RET_ERROR;
		}
		junction = *formula;
		formula++;
	}
	if (depth > 0) {
		fprintf(stderr,
"Missing ')' at end of formula '%s'!\n",
				origformula);
		term_free(first);
		return RET_ERROR;

	}
	if (*formula != '\0') {
		fprintf(stderr,
"Trailing garbage at end of term: '%s'\n",
				formula);
		term_free(first);
		return RET_ERROR;
	}
	*term_p = first;
	return RET_OK;
}

