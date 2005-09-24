/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "error.h"
#include "mprintf.h"
#include "optionsfile.h"

void optionsfile_parse(const char *confdir,const struct option *longopts,
		       void handle_option(int,const char *)) {
	FILE *f;
	char *filename;
	char buffer[1000];
	int linenr = 0;
	const struct option *option;

	filename = mprintf("%s/options",confdir);
	if( filename == NULL ) {
		fputs("Out of memory!\n",stderr);
		exit(EXIT_FAILURE);
	}

	f = fopen(filename,"r");
	if( f == NULL ) {
		free(filename);
		return;
	}
	while( fgets(buffer,999,f) != NULL ) {
		size_t l;
		char *optionname,*argument;

		linenr++;

		l = strlen(buffer);
		if( l == 0 || buffer[l-1] != '\n' ) {
			fprintf(stderr,"%s:%d: Ignoring too long (or incomplete) line.\n",filename,linenr);
			do { 
				if( fgets(buffer,999,f) == NULL )
					break;
				l = strlen(buffer);
			} while( l > 0 && buffer[l-1] != '\n' );
			continue;
		}
		do{
			buffer[l-1] = '\0';
			l--;
		} while( l > 0 && xisspace(buffer[l-1]));

		if( l == 0 )
			continue;

		optionname = buffer;
		while( *optionname != '\0' && xisspace(*optionname) )
			optionname++;
		assert( *optionname != '\0' );
		if( *optionname == '#' || *optionname == ';' )
			continue;
		argument = optionname;
		while( *argument != '\0' && !xisspace(*argument) )
			argument++;
		while( *argument != '\0' && xisspace(*argument) ) {
			*argument = '\0';
			argument++;
		}
		if( *argument == '\0' )
			argument = NULL;
		option = longopts;
		while( option->name != NULL && strcmp(option->name,optionname) != 0 )
			option++;
		if( option->name == NULL ) {
			fprintf(stderr,"%s:%d: unknown option '%s'!\n",
					filename,linenr,optionname);
			exit(EXIT_FAILURE);
		}
		if( option->has_arg==no_argument && argument != NULL ) {
			fprintf(stderr,"%s:%d: option '%s' has an unexpected argument '%s'!\n",
					filename,linenr,optionname,argument);
			exit(EXIT_FAILURE);
		}
		if( option->has_arg==required_argument && argument == NULL ) {
			fprintf(stderr,"%s:%d: option '%s' is missing an argument!\n",
					filename,linenr,optionname);
			exit(EXIT_FAILURE);
		}
		if( option->flag == NULL )
			handle_option(option->val,argument);
		else {
			*option->flag = option->val;
			handle_option(0,argument);
		}
	}
}
