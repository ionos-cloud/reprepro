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
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>
#include <db.h>
#include "error.h"
#include "mprintf.h"
#include "dirs.h"
#include "names.h"
#include "chunks.h"
#include "strlist.h"
#include "updates.h"

extern int verbose;


// typedef retvalue updatesaction(void *data,const char *chunk,const struct release *release,const char *name);

struct mydata {
	const struct release *release;
	const struct strlist *updates;
	void *data;
	updatesaction *action;
};

static retvalue processupdates(void *data,const char *chunk) {
	struct mydata *d = data;
	retvalue r;
	char *name;

	r = chunk_getvalue(chunk,"Name",&name);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Unexpected chunk in updates-file: '%s'.\n",chunk);
		return r;
	}
	if( !RET_IS_OK(r) )
		return r;

	if( strlist_in(d->updates,name) ) {

		if( verbose > 2 ) {
			fprintf(stderr,"processing '%s' for '%s'\n",name,d->release->codename);
		}
		// TODO: generate more information...
		// (like which distributions, architectures and components to update)
		d->action(d->data,chunk,d->release,name);

		free(name);
		return RET_OK;

	} else if( verbose > 5 ) {
		fprintf(stderr,"skipping '%s' in this run\n",name);
	}
	free(name);
	return RET_NOTHING;
}

retvalue updates_foreach_matching(const char *conf,const struct release *release,const struct strlist *updates,updatesaction action,void *data,int force) {
	retvalue result;
	char *fn;
	struct mydata mydata;

	mydata.release = release;
	mydata.updates = updates;
	mydata.data = data;
	mydata.action = action;
	
	fn = calc_dirconcat(conf,"updates");
	if( !fn ) 
		return RET_ERROR_OOM;
	
	result = chunk_foreach(fn,processupdates,&mydata,force);

	free(fn);
	return result;
}

