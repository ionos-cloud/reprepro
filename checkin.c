/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2003 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>
#include <db.h>
#include "error.h"
#include "strlist.h"
#include "md5sum.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "reference.h"
#include "packages.h"
#include "signature.h"
#include "sources.h"
#include "files.h"
#include "checkindsc.h"
#include "checkindeb.h"
#include "checkin.h"

extern int verbose;

/* Things to do when including a .changes-file:
 *  - Read in the chunk of the possible signed file.
 *    (In later versions possibly checking the signature)
 *  - Parse it, extracting:
 *  	+ Distribution
 * 	+ Source
 * 	+ Architecture
 * 	+ Binary
 * 	+ Version
 * 	+ ...
 * 	+ Files
 *  - Calculate what files are expectable...
 *  - Compare supplied filed with files expected.
 *  - (perhaps: write what was done and changes to some logfile)
 *  - add supplied files to the pool and register them in files.db
 *  - add the .dsc-files via checkindsc.c
 *  - add the .deb-filed via checkindeb.c
 *
 */

struct changes {
	/* Things read by changes_read: */
	char *distribution,*source,
	     *version;
	struct strlist architectures,
		       binaries,
		       files;
	char *control;
};

static void changes_free(struct changes *changes) {
	if( changes != NULL ) {
		free(changes->distribution);
		free(changes->source);
		free(changes->version);
	}
	free(changes);
}

static retvalue check(const char *filename,struct changes *changes,const char *field,int force) {
	retvalue r;

	r = chunk_checkfield(changes->control,field);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"In '%s': Missing '%s' field!\n",filename,field);
		if( !force )
			return RET_ERROR;
	}
	return r;
}

static retvalue changes_read(const char *filename,struct changes **changes,int force) {
	retvalue r;
	struct changes *c;

#define E(err,param...) { \
		if( r == RET_NOTHING ) { \
			fprintf(stderr,"In '%s': " err "\n",filename , ## param ); \
			r = RET_ERROR; \
	  	} \
		if( RET_WAS_ERROR(r) ) { \
			changes_free(c); \
			return r; \
		} \
	}
		
	c = calloc(1,sizeof(struct changes));
	if( c == NULL )
		return RET_ERROR_OOM;

	r = check(filename,c,"Format",force);
	if( RET_WAS_ERROR(r) )
		return r;
	
	r = check(filename,c,"Date",force);
	if( RET_WAS_ERROR(r) )
		return r;

	r = signature_readsignedchunk(filename,&c->control);
	if( RET_WAS_ERROR(r) )
		return r;

	r = chunk_getname(c->control,"Source",&c->source,0);
	E("Missing 'Source' field");

	r = chunk_getwordlist(c->control,"Binary",&c->binaries);
	E("Missing 'Binary' field");

	r = chunk_getwordlist(c->control,"Architecture",&c->architectures);
	E("Missing 'Architecture' field");

	r = chunk_getvalue(c->control,"Version",&c->version);
	E("Missing 'Version' field");

	r = chunk_getvalue(c->control,"Distribution",&c->distribution);
	E("Missing 'Distribution' field");

	r = check(filename,c,"Urgency",force);
	if( RET_WAS_ERROR(r) )
		return r;

	r = check(filename,c,"Maintainer",force);
	if( RET_WAS_ERROR(r) )
		return r;

	r = check(filename,c,"Description",force);
	if( RET_WAS_ERROR(r) )
		return r;

	r = check(filename,c,"Changes",force);
	if( RET_WAS_ERROR(r) )
		return r;

	r = chunk_getextralinelist(c->control,"Files",&c->files);
	E("Missing 'Files' field");

	r = check(filename,c,"Format",force);
	if( RET_WAS_ERROR(r) )
		return r;

	*changes = c;
	return RET_OK;
#undef E
}

/* insert the given .changes into the mirror in the <distribution>
 * if forcecomponent, forcesection or forcepriority is NULL
 * get it from the files or try to guess it. */
retvalue changes_add(const char *dbdir,DB *references,DB *filesdb,const char *mirrordir,const char *forcecomponent,const char *forcedsection,const char *forcepriority,struct distribution *distribution,const char *changesfilename,int force) {
	retvalue r;
	struct changes *changes;

	r = changes_read(changesfilename,&changes,force);
	if( RET_WAS_ERROR(r) )
		return r;
	
	// TODO: implement the rest
}
