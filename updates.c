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


// typedef retvalue updatesaction(void *data,const char *chunk,const struct release *release,struct update *update);

struct mydata {
	const char *updatesfile;
	int force;
	struct strlist upstreams;
	const struct release *release;
	updatesaction *action;
	void *data;
};

static retvalue processupdates(void *data,const char *chunk) {
	struct mydata *d = data;
	retvalue r;
	int i;
	struct update update;
	struct strlist componentlist;
	const struct strlist *components;
	int components_need_free;
	const char *component;
	char *origin,*destination;

	r = chunk_getvalue(chunk,"Name",&update.name);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Unexpected chunk in updates-file: '%s'.\n",chunk);
		return r;
	}
	if( !RET_IS_OK(r) )
		return r;

	r = RET_NOTHING;

	if( strlist_in(&d->upstreams,update.name) ) {

		if( verbose > 2 ) {
			fprintf(stderr,"processing '%s' for '%s'\n",update.name,d->release->codename);
		}
		/* * Check which suite to update from * */
		r = chunk_getvalue(chunk,"Suite",&update.suite_from);
		if( r == RET_NOTHING ) {
			/* if nothing given, try the one we know */
			update.suite_from = strdup(d->release->codename);
			if( !update.suite_from )
				r = RET_ERROR_OOM;
		} // TODO: check for some token to be repaced by the codename?
		  // i.e. */updates gets stable/updates unstable/updates ...
		if( !RET_WAS_ERROR(r) ) {

		/* * Check which architectures to update from * */
		r = chunk_getwordlist(chunk,"Architectures",&update.architectures);
		if( r == RET_NOTHING ) {
			/* if nothing given, try to get all the distribution knows */
			r = strlist_dup(&update.architectures,&d->release->architectures);
		}
		if( !RET_WAS_ERROR(r) ) {

		/* * Check which components to update from * */

		components_need_free = 1;
		components = &componentlist;
		r = chunk_getwordlist(chunk,"Components",&componentlist);
		if( r == RET_NOTHING ) {
			/* if nothing given, try to get all the distribution knows */
			components = &d->release->components;
			components_need_free = 0;
		}
		if( !RET_WAS_ERROR(r) ) {
			strlist_init(&update.components_from);
			strlist_init(&update.components_into);

			/* * Iterator over components to update * */
			r = RET_NOTHING;
			for( i = 0 ; i < components->count ; i++ ) {
				component = components->values[i];
				if( !(destination = strchr(component,'>')) || !*(destination+1)) {
					destination = strdup(component);
					origin = strdup(component);
				} else {
					origin = strndup(component,destination-component);
					destination = strdup(destination+1);
				}
				if( !origin || ! destination ) {
					r = RET_ERROR_OOM;
					break;
				}
				//TODO: check if in release.compoents 
				r = strlist_add(&update.components_from,origin);
				if( RET_WAS_ERROR(r) )
					break;
				r = strlist_add(&update.components_into,destination);
				if( RET_WAS_ERROR(r) )
					break;
			}

			if( !RET_WAS_ERROR(r) )
				r = d->action(d->data,chunk,d->release,&update);

			strlist_done(&update.components_from);
			strlist_done(&update.components_into);
			if( components_need_free )
				strlist_done(&componentlist);
		}
			strlist_done(&update.architectures);
		}
			free(update.suite_from);
		}

	} else if( verbose > 5 ) {
		fprintf(stderr,"skipping '%s' in this run\n",update.name);
	}
	free(update.name);
	return r;
}

static retvalue doupdate(void *data,const char *chunk,const struct release *release) {
	struct mydata *d = data;
	retvalue r;

	r = chunk_getwordlist(chunk,"Update",&d->upstreams);
	if( r == RET_NOTHING && verbose > 1 ) {
		fprintf(stderr,"Ignoring release '%s', as it describes no update\n",release->codename);
	}
	if( !RET_IS_OK(r) )
		return r;

	d->release = release;

	r = chunk_foreach(d->updatesfile,processupdates,d,d->force);

	strlist_done(&d->upstreams);

	return r;
}


retvalue updates_foreach(const char *confdir,int argc,char *argv[],updatesaction action,void *data,int force) {
	struct mydata mydata;
	retvalue result;

	mydata.updatesfile = calc_dirconcat(confdir,"updates");
	if( !mydata.updatesfile ) 
		return RET_ERROR_OOM;

	mydata.force=force;
	mydata.action=action;
	mydata.data=data;
	
	result = release_foreach(confdir,argc,argv,doupdate,&mydata,force);

	free((char*)mydata.updatesfile);

	return result;
}

