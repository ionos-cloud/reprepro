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
#include "strlist.h"
#include "dirs.h"
#include "names.h"
#include "chunks.h"
#include "signature.h"
#include "updates.h"

extern int verbose;

/* if found in a update-chunk, do no download or check release-files */
#define IGNORE_RELEASE "NoRelease"
/* defined if release.gpg should be checked, 
 * value not yet used, will be key-id or something like this */
#define VERIFY_RELEASE "ReleaseCheck"

// TODO: what about other signatures? Is hard-coding ".gpg" sensible?

struct updates_data {
	const char *updatesfile;
	int force;
	struct strlist upstreams;
	const struct distribution *distribution;
	updatesaction *action;
	void *data;
};

static retvalue splitComponents(struct strlist *components_from,
				struct strlist *components_into,
				const struct strlist *components,
				const struct strlist *released) {
	retvalue r;
	int i;
	const char *component,*dest;
	char *origin,*destination;

	/* * Iterator over components to update * */
	r = RET_NOTHING;
	for( i = 0 ; i < components->count ; i++ ) {
		component = components->values[i];
		if( !(dest = strchr(component,'>')) || !*(dest+1)) {
			destination = strdup(component);
			origin = strdup(component);
		} else {
			origin = strndup(component,dest-component);
			destination = strdup(dest+1);
		}
		if( !origin || ! destination ) {
			free(origin);free(destination);
			return RET_ERROR_OOM;
		}
		//TODO: check if in distribution.compoents 
		r = strlist_add(components_from,origin);
		if( RET_WAS_ERROR(r) )
			return r;
		r = strlist_add(components_into,destination);
		if( RET_WAS_ERROR(r) )
			return r;
		r = RET_OK;
	}
	return r;
}

static retvalue calcComponentsToUpdate(struct strlist *components_from,
					struct strlist *components_into,
					const char *updatechunk,
					const struct strlist *releasedcomponents) {
	retvalue r;
	struct strlist componentlist;

	assert( components_from != NULL && components_into != NULL );

	/* First look what is to do. If there is nothing special it's easy... */
	r = chunk_getwordlist(updatechunk,"Components",&componentlist);
	if( RET_WAS_ERROR(r) ) {
		return r;
	} else
	if( r == RET_NOTHING ) {
		/* if nothing given, we just do everything... */
		r = strlist_dup(components_from,releasedcomponents);
		if( RET_WAS_ERROR(r) )
			return r;
		r = strlist_dup(components_into,releasedcomponents);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(components_from);
			return r;
		}
		return RET_OK;
	}

	/* initializing components_from,components_into and getting components: */
	r = strlist_init(components_from);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&componentlist);
		return r;
	}
	r = strlist_init(components_into);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(components_from);
		strlist_done(&componentlist);
		return r;
	}

	r = strlist_init(components_from);
	if( RET_WAS_ERROR(r) )
		return r;
	r = strlist_init(components_into);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(components_from);
		return r;
	}

	/* * Iterator over components to update * */
	r = splitComponents(components_from,components_into,&componentlist,releasedcomponents);

	if( !RET_IS_OK(r) ) {
		strlist_done(components_into);
		strlist_done(components_from);
	}
	strlist_done(&componentlist);
	return r;
}

static retvalue processupdates(void *data,const char *chunk) {
	struct updates_data *d = data;
	retvalue r;
	struct update update;

	r = chunk_getvalue(chunk,"Name",&update.name);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Unexpected chunk in updates-file: '%s'.\n",chunk);
		return RET_ERROR;
	}
	if( !RET_IS_OK(r) )
		return r;

	if( !strlist_in(&d->upstreams,update.name) ) {
//		fprintf(stderr,"skipping '%s' in this run\n",update.name);
		free(update.name);
		return RET_NOTHING;
	}

	if( verbose > 2 ) {
		fprintf(stderr,"processing '%s' for '%s'\n",
				update.name,d->distribution->codename);
	}
	/* * Check which suite to update from * */
	r = chunk_getvalue(chunk,"Suite",&update.suite_from);
	if( r == RET_NOTHING ) {
		/* if nothing given, try the one we know */
		update.suite_from = strdup(d->distribution->codename);
		if( !update.suite_from )
			r = RET_ERROR_OOM;
	} 
	// TODO: check for some token to be repaced by the codename?
	// i.e. */updates gets stable/updates unstable/updates ...
	if( !RET_WAS_ERROR(r) ) {

		/* * Check which architectures to update from * */
		r = chunk_getwordlist(chunk,"Architectures",&update.architectures);
		if( r == RET_NOTHING ) {
			/* if nothing given, try to get all the distribution knows */
			r = strlist_dup(&update.architectures,&d->distribution->architectures);
		}
		if( !RET_WAS_ERROR(r) ) {

			/* * Check which components to update from * */

			r = calcComponentsToUpdate(
					&update.components_from,
					&update.components_into,
					chunk,&d->distribution->components);

			if( RET_IS_OK(r) ) {
				r = d->action(d->data,chunk,d->distribution,&update);

				strlist_done(&update.components_into);
				strlist_done(&update.components_from);
			} 
			strlist_done(&update.architectures);
		}
		free(update.suite_from);
	}

	free(update.name);
	return r;
}

static retvalue doupdate(void *data,const char *chunk,const struct distribution *distribution) {
	struct updates_data *d = data;
	retvalue r;

	r = chunk_getwordlist(chunk,"Update",&d->upstreams);
	if( r == RET_NOTHING && verbose > 1 ) {
		fprintf(stderr,"Ignoring distribution '%s', as it describes no update\n",distribution->codename);
	}
	if( !RET_IS_OK(r) )
		return r;

	d->distribution = distribution;

	r = chunk_foreach(d->updatesfile,processupdates,d,d->force,0);

	strlist_done(&d->upstreams);

	return r;
}


retvalue updates_foreach(const char *confdir,int argc,char *argv[],updatesaction action,void *data,int force) {
	struct updates_data mydata;
	retvalue result;

	mydata.updatesfile = calc_dirconcat(confdir,"updates");
	if( !mydata.updatesfile ) 
		return RET_ERROR_OOM;

	mydata.force=force;
	mydata.action=action;
	mydata.data=data;
	
	result = distribution_foreach(confdir,argc,argv,doupdate,&mydata,force);

	free((char*)mydata.updatesfile);

	return result;
}


/******************* Fetch all Lists for an update **********************/

inline static retvalue adddownload(struct strlist *d,char *remote,char *local) {
	retvalue r;

	// TODO: first check, if the specified file is already there,
	// (hm, perhaps strlist is bad here and strtuplelist would be better)

	if( !local || !remote ) {
		free(remote);free(local);
		return RET_ERROR_OOM;
	} else {
		r = strlist_add(d,remote);
		if( RET_WAS_ERROR(r) ) {
			free(local);
			return r;
		}
		r = strlist_add(d,local);
		return r;
	}
}

retvalue updates_calcliststofetch(struct strlist *todownload,
		/* where to save to file */
		const char *listdir, const char *codename,const char *update,const char *updatechunk,
		/* where to get it from */
		const char *suite_from,
		/* what parts to get */
		const struct strlist *components_from,
		const struct strlist *architectures
		) {

	const char *comp,*arch;
	char *toget,*saveas;
	retvalue r;
	int i,j;

	r = chunk_gettruth(updatechunk,IGNORE_RELEASE);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		toget = mprintf("dists/%s/Release",suite_from);
		saveas = calc_downloadedlistfile(listdir,codename,update,"Release","data");
		r = adddownload(todownload,toget,saveas);
		if( RET_WAS_ERROR(r) )
			return r;

		// TODO: check if signatures are to be made...

		toget = mprintf("dists/%s/Release.gpg",suite_from);
		saveas = calc_downloadedlistfile(listdir,codename,update,"Release","gpg");
		r = adddownload(todownload,toget,saveas);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	/* * Iterate over components to update * */
	for( i = 0 ; i < components_from->count ; i++ ) {
		comp = components_from->values[i];

		toget = mprintf("dists/%s/%s/source/Sources.gz",suite_from,comp);
		saveas = calc_downloadedlistfile(listdir,codename,update,comp,"source");
		r = adddownload(todownload,toget,saveas);
		if( RET_WAS_ERROR(r) )
			return r;


		for( j = 0 ; j < architectures->count ; j++ ) {
			arch =architectures->values[j];

			toget = mprintf("dists/%s/%s/binary-%s/Packages.gz",suite_from,comp,arch);
			saveas = calc_downloadedlistfile(listdir,codename,update,comp,arch);
			r = adddownload(todownload,toget,saveas);
			if( RET_WAS_ERROR(r) )
				return r;
		}

	}
	return RET_OK;
}

/******************* Check fetched lists for update *********************/
static retvalue checkpackagelists(struct strlist *checksums,
		/* where to find the files to test: */
		const char *listdir, const char *codename,const char *update, 
		/* where to get it from */
		const char *suite_from,
		/* what parts to check */
		const struct strlist *components_from,
		const struct strlist *architectures
		) {
	const char *comp,*arch;
	retvalue result,r;
	char *name,*totest;
	int i,j;
	
	result = RET_NOTHING;
	for( i = 0 ; i < components_from->count ; i++ ) {
		comp = components_from->values[i];

		name = mprintf("%s/source/Sources.gz",comp);
		totest = calc_downloadedlistfile(listdir,codename,update,comp,"source");
		r = release_check(checksums,name,totest);
		free(name); free(totest);
		RET_UPDATE(result,r);

		
		for( j = 0 ; j < architectures->count ; j++ ) {
			arch = architectures->values[j];

			name = mprintf("%s/binary-%s/Packages.gz",comp,arch);
			totest = calc_downloadedlistfile(listdir,codename,update,comp,arch);
			r = release_check(checksums,name,totest);
			free(name); free(totest);
			RET_UPDATE(result,r);
		}
		
	}
	return result;
}


retvalue updates_checkfetchedlists(const struct update *update,const char *updatechunk,const char *listdir,const char *codename) {
	char *releasefile,*checkoptions;
	struct strlist checksums;
	retvalue r;

	r = chunk_gettruth(updatechunk,IGNORE_RELEASE);
	if( RET_IS_OK(r) ) {
		return RET_NOTHING;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	assert( r == RET_NOTHING );

	r = chunk_getvalue(updatechunk,VERIFY_RELEASE,&checkoptions);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING )
		checkoptions = NULL;

	releasefile = calc_downloadedlistfile(listdir,codename,update->name,"Release","data");
	if( releasefile == NULL ) {
		free(checkoptions);
		return RET_ERROR_OOM;
	}

	/* if there is nothing said, then there is nothing to check... */
	if( checkoptions != NULL ) {
		char *gpgfile;

		gpgfile = calc_downloadedlistfile(listdir,codename,update->name,"Release","gpg");
		if( gpgfile == NULL ) {
			free(releasefile);
			free(checkoptions);
			return RET_ERROR_OOM;
		}
		r = signature_check(checkoptions,gpgfile,releasefile);
		free(gpgfile);
		free(checkoptions);
	}

	if( RET_WAS_ERROR(r) ) {
		free(releasefile);
		return r;
	}

	r = release_getchecksums(releasefile,&checksums);
	free(releasefile);
	if( RET_WAS_ERROR(r) )
		return r;

	r = checkpackagelists(&checksums,listdir,codename,update->name,update->suite_from,&update->components_from,&update->architectures);

	strlist_done(&checksums);

	return r;
}
