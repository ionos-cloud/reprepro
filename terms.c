/*  This file is part of "reprepro"
 *  Copyright (C) 2004 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <config.h>

#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "terms.h"

void term_free(term *term) {
	while( term != NULL ) {
		struct term_atom *next = term->next;
		free(term->key);
		free(term->comparewith);
		strlist_done(&term->architectures);
		free(term);
		term = next;
	}
}

static retvalue parseatom(const char **formula,struct term_atom **atom,int options) {
	struct term_atom *a;
	const char *f = *formula;
#define overspace() while( *f && isspace(*f) ) f++
	const char *keystart,*keyend;
	const char *valuestart,*valueend;
	enum term_comparison comparison = tc_none;


	overspace();
	keystart = f;
	names_overpkgname(&f);
	keyend = f;
	if( keystart == keyend ) {
		*formula = f;
		return RET_NOTHING;
	}
	overspace();
	if( options & T_VERSION && *f == '(' ) {
		f++;
		overspace();
		switch( *f ) {
			case '>':
				f++;
				if( *f == '=' ) {
					comparison = tc_moreorequal;
					f++;
				} else if( *f == '>' ) {
					comparison = tc_strictmore;
					f++;
				} else {
					comparison = tc_moreorequal;
					fprintf(stderr,"Warning: Found a '(>' without '=' or '>'  in '%s'(beginning cut), will be treated as '>='.\n",*formula);
				}
				break;
			case '<':
				f++;
				if( *f == '=' ) {
					comparison = tc_lessorequal;
					f++;
				} else if( *f == '>' ) {
					comparison = tc_strictless;
					f++;
				} else {
					comparison = tc_lessorequal;
					fprintf(stderr,"Warning: Found a '(<' without '=' or '<'  in '%s'(begin cut), will be treated as '<='.\n",*formula);
				}
				break;
			case '=':
				f++;
				if( *f != '=' ) {
					*formula = f;
					return RET_NOTHING;
				}
				f++;
				comparison = tc_equal;
				break;
			case '!':
				if( options & T_NOTEQUAL ) {
					f++;
					if( *f != '=' ) {
						*formula = f;
						return RET_NOTHING;
					}
					f++;
					comparison = tc_notequal;
					break;
				}
				// no break here...
			default:
				*formula = f;
				return RET_NOTHING;
		}
		overspace();
		valueend = valuestart = f;
		while( *f && *f != ')' ) {
			valueend = f+1;
			f++;
			while( *f && isspace(*f) )
				f++;
		}
		if( *f != ')' || valueend == valuestart ) {
			*formula = f;
			return RET_NOTHING;
		}
		f++;

	} else {
		comparison = '\0';
		valuestart = valueend = NULL;
	}
	overspace();
	if( options & T_ARCHITECTURES && *f == '[' ) {
		//TODO: implement this one...
		assert( "Not yet implemented!" == NULL); 
	}

	a = calloc(1,sizeof(struct term_atom));
	if( a == NULL )
		return RET_ERROR_OOM;
	a->negated = FALSE; // not yet supported..., what char would that be?
	a->key = strndup(keystart,keyend-keystart);
	if( a->key == NULL ) {
		term_free(a);
		return RET_ERROR_OOM;
	}
	a->comparison = comparison;
	if( comparison != tc_none ) {
		a->comparewith =  strndup(valuestart,valueend-valuestart);
		if( a->comparewith == NULL ) {
			term_free(a);
			return RET_ERROR_OOM;
		}
	}

	//TODO: here architectures, too

	*atom = a;
	*formula = f;
	return RET_OK;
#undef overspace
}

/* as this are quite special BDDs (a atom beeing false cannot make it true), 
 * the places where True and False can be found are
 * quite easy and fast to find: */

static void orterm(term *termtochange, term *termtoor) {
	struct term_atom *p = termtochange;

	while( p ) {
		while( p->nextiffalse )
			p = p->nextiffalse;
		p->nextiffalse= termtoor;
		p = p->nextiftrue;
	}
}
static void andterm(term *termtochange, term *termtoand) {
	struct term_atom *p = termtochange;

	while( p ) {
		while( p->nextiftrue )
			p = p->nextiftrue;
		p->nextiftrue = termtoand;
		p = p->nextiffalse;
	}
}

retvalue term_compile(term **term, const char *origformula, int options) {
	const char *formula = origformula;
	/* for the global list */ 
	struct term_atom *first,*last;
	/* the atom just read */
	struct term_atom *atom;
	struct {struct term_atom *firstinand,*firstinor;} levels[50];
	int lastinitializeddepth=-1;
	int depth=0;
	retvalue r;
	int i;
	//TODO: ???
	int atbeginning = 1;
	char junction = '\0';
	
	if( options & T_ARCHITECTURES  ) {
		//TODO: implement this one...
		assert( "Not yet implemented!" == NULL); 
	}

#define overspace() while( *formula && isspace(*formula) ) formula++

	lastinitializeddepth=-1;
	depth=0;
	first = last = NULL;

	while( 1 ) {
		overspace();
		while( *formula == '(' && (options & T_BRACKETS)) {
			depth++;formula++;
			overspace();
		}
		if( depth >= 50 ) {
			term_free(first);
			fprintf(stderr,"Nested too deep: '%s'!\n",origformula);
			return RET_ERROR;
		}
		r = parseatom(&formula,&atom,options);
		if( r == RET_NOTHING ) {
			if( *formula == '\0' )
				fprintf(stderr,"Unexpected end of string parsing formula '%s'!\n",origformula);
			else
				fprintf(stderr,"Unexpected character '%c' parsing formula '%s'!\n",*formula,origformula);

			r = RET_ERROR;
		}
		if( RET_WAS_ERROR(r) ) {
			term_free(first);
			return r;
		}
		for( i=lastinitializeddepth+1 ; i <= depth ; i ++ ) {
			levels[i].firstinand = atom;
			levels[i].firstinor = atom;
		}
		if( junction != '\0' ) {
			assert(lastinitializeddepth >= 0 );
			assert( first != NULL );
			last->next = atom;
			last = atom;
			if( junction == ',' ) {
				andterm(levels[lastinitializeddepth].firstinand,atom);
				levels[lastinitializeddepth].firstinand = atom;
				levels[lastinitializeddepth].firstinor = atom;
			} else {
				assert( junction == '|' );
				orterm(levels[lastinitializeddepth].firstinor,atom);
				levels[lastinitializeddepth].firstinor = atom;
			}
		} else {
			assert(lastinitializeddepth == -1 );
			assert( first == NULL );
			first = last = atom;
		}
		lastinitializeddepth = depth;
		overspace();
		if( *formula == ')' && (options & T_BRACKETS)) {
			formula++;
			if( depth > 0 ) {
				depth--;
				lastinitializeddepth = depth;
			} else {
				fprintf(stderr,"Too many ')'s in '%s'!\n",origformula);
				term_free(first);
				return RET_ERROR;
			}
			atbeginning = 1;
			overspace();
		}
		overspace();
		if( !*formula )
			break;
		if( *formula != ',' && ( *formula != '|' || (options & T_OR)==0 )) {
			fprintf(stderr,"Unexpected character '%c' within '%s'!\n",*formula,origformula);
			term_free(first);
			return RET_ERROR;
		}
		junction = *formula;
		formula++;
	}
	if( depth > 0 ) {
		fprintf(stderr,"Missing ')' at end of formula '%s'!\n",origformula);
		term_free(first);
		return RET_ERROR;

	}
	if( *formula != '\0' ) {
		fprintf(stderr,"Trailing garbage at end of term: '%s'\n",formula);
		term_free(first);
		return RET_ERROR;
	}
	*term = first;
	return RET_OK;
}
