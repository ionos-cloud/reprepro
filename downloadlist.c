/*  This file is part of "mirrorer" (TODO: find better title)
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
#include "aptmethod.h"
#include "downloadlist.h"

struct downloaditem {
	struct downloaditem *nextinupstream;
	// todo: what is best, some tree, balanced tree, linear?
	struct downloaditem *next,*prev;
	char *filekey;
	char *origfile;
	char *md5sum;
};

struct download_upstream {
	struct downloadlist *list;
	struct download_upstream *next;
	char *method;
	char *config;
	struct downloaditem *items;
};
struct downloadlist {
	char *pooldir;
	struct download_upstream *upstreams;
};


/* Initialize a new download session */
retvalue downloadlist_initialize(struct downloadlist **download,const char *pooldir);

/* free all memory, cancel all queued downloads */
retvalue downloadlist_done(struct downloadlist *downloadlist);

/* try to fetch and register all queued files */
retvalue downloadlist_run(struct downloadlist *list,const char *methodir,int force) {
	struct aptmethodrun *run;
	struct download_upstream *upstream;
	retvalue r;

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) )
		return r;
	for( upstream = list->upstreams ;upstream; upstream = upstream->next) {
		struct aptmethod *method;
		struct downloaditem *item;

		r = aptmethod_newmethod(run,upstream->method,&method);
		if( RET_WAS_ERROR(r) ) {
			aptmethod_cancel(run);
			return r;
		}
		for( item=upstream->items;item;item=item->nextinupstream) {
			char *destination;

			destination = calc_dirconcat(run->pooldir,item->filekey);
			if( destination == NULL ) {
				aptmethod_cancel(run);
				return RET_ERROR_OOM;
			}
			r = aptmethod_queuefile(method,item->origfile,
					destination,item->md5sum);
			free(destination);
			if( RET_WAS_ERROR(r) ) {
				aptmethod_cancel(run);
				return r;
			}
		}
		
		
	}
	r = aptmethod_download(run,methoddir);
	if( RET_WAS_ERROR(r) && !force )
		return r;
	// TODO: add files to database. (perhaps even in !force-case?)
	...
	return RET_ERROR;
}

/* add a new upstream to download files from,
retvalue downloadlist_newupstream(struct downloadlist *download,
		const char *method,const char *config,struct upstream **upstream);
		
/* queue a new file to be downloaded: 
 * results in RET_ERROR_WRONG_MD5, if someone else already asked
 * for the same destination with other md5sum created. */
retvalue downloadlist_add(struct download_upstream *upstream,const char *orig,const char *dest,const char *md5sum);
