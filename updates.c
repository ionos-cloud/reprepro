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
#include "aptmethod.h"
#include "updates.h"
#include "distribution.h"

// TODO: what about other signatures? Is hard-coding ".gpg" sensible?



extern int verbose;

/* if found in a update-chunk, do no download or check release-files */
#define IGNORE_RELEASE "IgnoreRelease"
/* defined if release.gpg should be checked, 
 * value not yet used, will be key-id or something like this */
#define VERIFY_RELEASE "VerifyRelease"

/* the data for some upstream part to get updates from, this can
 * happen in two versions, either as pattern with some fields NULL,
 * or empty, and the other is filled out with the values for an actual
 * upstream used for a specific suite... */
struct update_upstream {
	struct update_upstream *next;
	//e.g. "Name: woody"
	char *name;
	//e.g. "Method: ftp://ftp.uni-freiburg.de/pub/linux/debian"
	char *method;
	//e.g. "Suite: woody" or "Suite: <asterix>/updates" (NULL means "*")
	char *suite_from;
	//e.g. "IgnoreRelease: Yes" for 1 (default is 0)
	int ignorerelease;
	//e.g. "ReleaseCheck: B629A24C38C6029A" (NULL means not check)
	char *verifyrelease;
	//e.g. "Architectures: i386 sparc mips" (empty means all)
	struct strlist architectures;
	//e.g. "Components: main>main non-free>non-free contrib>contrib"
	// (empty means all)
	struct strlist components_from;
	struct strlist components_into;
	// distribution to go into (NULL for pattern)
	const struct distribution *distribution;
};

void update_upstream_free(struct update_upstream *update) {
	if( update == NULL )
		return;
	free(update->name);
	free(update->method);
	free(update->suite_from);
	free(update->verifyrelease);
	strlist_done(&update->architectures);
	strlist_done(&update->components_from);
	strlist_done(&update->components_into);
	free(update);
}

static retvalue splitcomponents(struct strlist *components_from,
				struct strlist *components_into,
				const struct strlist *components) {
	retvalue r;
	int i;
	const char *component,*dest;
	char *origin,*destination;

	r = strlist_init(components_from);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	r = strlist_init(components_into);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(components_from);
		return r;
	}

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
			strlist_done(components_from);
			strlist_done(components_into);
			return RET_ERROR_OOM;
		}
		//TODO: check if in distribution.compoents 
		r = strlist_add(components_from,origin);
		if( RET_WAS_ERROR(r) ) {
			free(destination);
			strlist_done(components_from);
			strlist_done(components_into);
			return r;
		}
		r = strlist_add(components_into,destination);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(components_from);
			strlist_done(components_into);
			return r;
		}
		r = RET_OK;
	}
	return r;
}

inline static retvalue parse_pattern(const char *chunk, struct update_upstream **upstream) {
	struct update_upstream *update;
	struct strlist componentslist;
	retvalue r;

	update = calloc(1,sizeof(struct update_upstream));
	if( update == NULL )
		return RET_ERROR_OOM;
	r = chunk_getvalue(chunk,"Name",&update->name);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Unexpected chunk in updates-file: '%s'.\n",chunk);
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(update);
		return r;
	}
	if( verbose > 30 ) {
		fprintf(stderr,"parsing update-chunk '%s'\n",update->name);
	}

	/* * Look where we are getting it from: * */

	r = chunk_getvalue(chunk,"Method",&update->method);
	if( !RET_IS_OK(r) ) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"No Method found in update-block!\n");
			r = RET_ERROR_MISSING;
		}
		update_upstream_free(update);
		return r;
	}

	/* * Check which suite to update from * */
	r = chunk_getvalue(chunk,"Suite",&update->suite_from);
	if( RET_WAS_ERROR(r) ) {
		update_upstream_free(update);
		return r;
	}
	if( r == RET_NOTHING )
		update->suite_from = NULL;

	/* * Check which architectures to update from * */
	r = chunk_getwordlist(chunk,"Architectures",&update->architectures);
	if( RET_WAS_ERROR(r) ) {
		update_upstream_free(update);
		return r;
	}
	if( r == RET_NOTHING ) {
		update->architectures.count = 0;
	}

	/* * Check which components to update from * */
	r = chunk_getwordlist(chunk,"Components",&componentslist);
	if( RET_WAS_ERROR(r) ) {
		update_upstream_free(update);
		return r;
	}
	if( r == RET_NOTHING ) {
		update->components_from.count = 0;
		update->components_into.count = 0;
	} else {
		r = splitcomponents(&update->components_from,
				&update->components_into,&componentslist);
		strlist_done(&componentslist);
		if( RET_WAS_ERROR(r) ) {
			update_upstream_free(update);
			return r;
		}
	}

	/* * Check if we should get the Release-file * */
	r = chunk_gettruth(chunk,IGNORE_RELEASE);
	if( RET_WAS_ERROR(r) ) {
		update_upstream_free(update);
		return r;
	}
	update->ignorerelease = (r == RET_OK);

	/* * Check if we should get the Release.gpg file * */
	r = chunk_getvalue(chunk,VERIFY_RELEASE,&update->verifyrelease);
	if( RET_WAS_ERROR(r) ) {
		update_upstream_free(update);
		return r;
	}
	if( r == RET_NOTHING )
		update->verifyrelease = NULL;

	*upstream = update;
	return RET_OK;
}


static retvalue instance_pattern(const struct update_upstream *pattern,
		const struct distribution *distribution,
		struct update_upstream **upstream) {

	struct update_upstream *update;
	retvalue r;

	update = calloc(1,sizeof(struct update_upstream));
	if( update == NULL )
		return RET_ERROR_OOM;

	update->name = strdup(pattern->name);
	if( update->name == NULL ) {
		update_upstream_free(update);
		return RET_ERROR_OOM;
	}
	update->method = strdup(pattern->method);
	if( update->method == NULL ) {
		update_upstream_free(update);
		return RET_ERROR_OOM;
	}
	update->method = strdup(pattern->method);
	if( update->method == NULL ) {
		update_upstream_free(update);
		return RET_ERROR_OOM;
	}
	if( pattern->verifyrelease == NULL )
		update->verifyrelease = NULL;
	else {
		update->verifyrelease = strdup(pattern->verifyrelease);
		if( update->verifyrelease == NULL ) {
			update_upstream_free(update);
			return RET_ERROR_OOM;
		}
	}
	update->ignorerelease = pattern->ignorerelease;

	if( pattern->suite_from == NULL || strcmp(pattern->suite_from,"*")== 0)
		update->suite_from = strdup(distribution->codename);
	else {
		if( pattern->suite_from[0] == '*' &&
				pattern->suite_from[1] == '/' )
			update->suite_from = calc_dirconcat(distribution->codename,pattern->suite_from+2);
		else if( index(pattern->suite_from,'*') == NULL )
			update->suite_from = strdup(pattern->suite_from);
		else {
			//TODO: implement this...
			fprintf(stderr,"Unsupported pattern '%s'\n",pattern->suite_from);
			update_upstream_free(update);
			return RET_ERROR;
		}
	}
	if( update->suite_from == NULL ) {
		update_upstream_free(update);
		return RET_ERROR_OOM;
	}

	if( pattern->architectures.count == 0 )
		r = strlist_dup(&update->architectures,&distribution->architectures);
	else
		r = strlist_dup(&update->architectures,&pattern->architectures);
	if( RET_WAS_ERROR(r) ) {
		update_upstream_free(update);
		return r;
	}
	if( pattern->components_from.count == 0 )
		r = strlist_dup(&update->components_from,&distribution->components);
	else
		r = strlist_dup(&update->components_from,&pattern->components_from);
	if( RET_WAS_ERROR(r) ) {
		update_upstream_free(update);
		return r;
	}
	if( pattern->components_into.count == 0 )
		r = strlist_dup(&update->components_into,&distribution->components);
	else
		r = strlist_dup(&update->components_into,&pattern->components_into);
	if( RET_WAS_ERROR(r) ) {
		update_upstream_free(update);
		return r;
	}
	update->distribution = distribution;
	
	
	*upstream = update;
	return RET_OK;
}

static retvalue parsechunk(void *data,const char *chunk) {
	struct update_upstream *update;
	struct update_upstream **upstreams = data;
	retvalue r;

	r = parse_pattern(chunk,&update);
	if( RET_IS_OK(r) ) {
		update->next = *upstreams;
		*upstreams = update;
	}
	return r;
}

retvalue updates_getpatterns(const char *confdir,struct update_upstream **patterns,int force) {
	char *updatesfile;
	struct update_upstream *update = NULL;
	retvalue r;

	updatesfile = calc_dirconcat(confdir,"updates");
	if( !updatesfile ) 
		return RET_ERROR_OOM;
	r = chunk_foreach(updatesfile,parsechunk,&update,force,0);
	free(updatesfile);
	if( RET_IS_OK(r) )
		*patterns = update;
	return r;
}

static retvalue getupstreams(const struct update_upstream *patterns,const struct distribution *distribution,struct update_upstream **upstreams) {
	const struct update_upstream *pattern;
	struct update_upstream *updates = NULL;
	retvalue result;

	result = RET_NOTHING;

	for( pattern = patterns ; pattern ; pattern = pattern->next ) {
		struct update_upstream *update;
		retvalue r;

		if( !strlist_in(&distribution->updates,pattern->name) )
			continue;
		r = instance_pattern(pattern,distribution,&update);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		if( RET_IS_OK(r) ) {
			update->next = updates;
			updates = update;
		}
	}
	if( RET_WAS_ERROR(result) ) {
		while( updates ) {
			struct update_upstream *update;

			update = updates;
			updates = updates->next;
			update_upstream_free(update);
		}
	} else {
		*upstreams = updates;
	}
	return result;
}

retvalue updates_getupstreams(const struct update_upstream *patterns,const struct distribution *distributions,struct update_upstream **upstreams) {
	struct update_upstream *updates = NULL;
	const struct distribution *distribution;
	retvalue result;

	result = RET_NOTHING;

	for( distribution = distributions ; distribution ; distribution = distribution->next ) {
		struct update_upstream *update;
		retvalue r;

		r = getupstreams(patterns,distribution,&update);

		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		if( RET_IS_OK(r) ) {
			struct update_upstream *last;

			//TODO: make sure count==count
	
			// TODO: ensure this:
			assert(update);
			last = update;
			while( last->next )
				last = last->next;
			last->next = updates;
			updates = update;
		}
	}
	if( RET_WAS_ERROR(result) ) {
		while( updates ) {
			struct update_upstream *update;

			update = updates;
			updates = updates->next;
			update_upstream_free(update);
		}
	} else {
		*upstreams = updates;
	}
	return result;
}

/******************* Fetch all Lists for an update **********************/
retvalue queuelists(struct aptmethodrun *run,const char *listdir,const struct update_upstream *upstream) {
	char *toget,*saveas;
	int i,j;
	struct aptmethod *method;
	retvalue r;

	r = aptmethod_newmethod(run,upstream->method,&method);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	if( !upstream->ignorerelease ) {
		toget = mprintf("dists/%s/Release",upstream->suite_from);
		saveas = calc_downloadedlistfile(listdir,upstream->distribution->codename,upstream->name,"Release","data");
		r = aptmethod_queuefile(method,toget,saveas,NULL);
		if( RET_WAS_ERROR(r) )
			return r;

		if( upstream->verifyrelease != NULL ) {
			toget = mprintf("dists/%s/Release.gpg",upstream->suite_from);
			saveas = calc_downloadedlistfile(listdir,upstream->distribution->codename,upstream->name,"Release","gpg");
			r = aptmethod_queuefile(method,toget,saveas,NULL);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}

	/* * Iterate over components to update * */
	for( i = 0 ; i < upstream->components_from.count ; i++ ) {
		const char *comp = upstream->components_from.values[i];

		toget = mprintf("dists/%s/%s/source/Sources.gz",upstream->suite_from,comp);
		saveas = calc_downloadedlistfile(listdir,upstream->distribution->codename,upstream->name,comp,"source");
		r = aptmethod_queuefile(method,toget,saveas,NULL);
		if( RET_WAS_ERROR(r) )
			return r;

		for( j = 0 ; j < upstream->architectures.count ; j++ ) {
			const char *arch = upstream->architectures.values[j];

			toget = mprintf("dists/%s/%s/binary-%s/Packages.gz",upstream->suite_from,comp,arch);
			saveas = calc_downloadedlistfile(listdir,upstream->distribution->codename,upstream->name,comp,arch);
			r = aptmethod_queuefile(method,toget,saveas,NULL);
			if( RET_WAS_ERROR(r) )
				return r;
		}

	}
	return RET_OK;
}

retvalue updates_queuelists(struct aptmethodrun *run,const char *listdir,struct update_upstream *upstreams) {
	retvalue result,r;
	struct update_upstream *upstream;

	result = RET_NOTHING;
	for( upstream=upstreams ; upstream ; upstream=upstream->next ) {
		r = queuelists(run,listdir,upstream);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	return result;
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

static retvalue checklists(const char *listdir,const struct update_upstream *upstream) {
	char *releasefile;
	struct strlist checksums;
	retvalue r;

	if( upstream->ignorerelease )
		return RET_NOTHING;

	releasefile = calc_downloadedlistfile(listdir,upstream->distribution->codename,upstream->name,"Release","data");
	if( releasefile == NULL ) {
		return RET_ERROR_OOM;
	}

	/* if there is nothing said, then there is nothing to check... */
	if( upstream->verifyrelease != NULL ) {
		char *gpgfile;

		gpgfile = calc_downloadedlistfile(listdir,upstream->distribution->codename,upstream->name,"Release","gpg");
		if( gpgfile == NULL ) {
			free(releasefile);
			return RET_ERROR_OOM;
		}
		r = signature_check(upstream->verifyrelease,gpgfile,releasefile);
		free(gpgfile);
		if( RET_WAS_ERROR(r) ) {
			free(releasefile);
			return r;
		}
	}

	r = release_getchecksums(releasefile,&checksums);
	free(releasefile);
	if( RET_WAS_ERROR(r) )
		return r;

	r = checkpackagelists(&checksums,listdir,upstream->distribution->codename,upstream->name,upstream->suite_from,&upstream->components_from,&upstream->architectures);

	strlist_done(&checksums);

	return r;
}

retvalue updates_checklists(const char *listdir,const struct update_upstream *upstreams,int force) {
	retvalue result,r;
	const struct update_upstream *upstream;

	result = RET_NOTHING;
	for( upstream=upstreams ; upstream ; upstream=upstream->next ) {
		r = checklists(listdir,upstream);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && !force )
			break;
	}
	return result;
}
