/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004 Bernhard R. Link
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
#include "upgradelist.h"
#include "distribution.h"

// TODO: what about other signatures? Is hard-coding ".gpg" sensible?

extern int verbose;

/* The data structures of this one: ("u_" is short for "update_")

updates_getpatterns read a list of patterns from <confdir>/updates:

   u_pattern --> u_pattern --> u_pattern --> NULL
       / \           / \          / \ / \
        |             \            |   |
	 \             ----\       |   |
	  ------------     |       |   |
	               \   .       |   .
                        |          |
updates_getupstreams instances them for a given distribution:
                        |          |
   distribution --> u_origin -> u_origin --> NULL
      |   |          / \ / \    / \ / \
      |  \ /          |   |      |   |
      | u_target -> u_index -> u_index -> NULL
      |   |              |        |
      |  \ /             |        |
      | u_target -> u_index -> u_index -> NULL
      |   |
      |  \ /                     
      |  NULL              .           .
     \ /                   |           |
   distribution ---> u_origin -> u_origin -> NULL
      |   |            / \          / \
      |  \ /            |            |
      | u_target --> u_index ---> u_index -> NULL
      |   |
      |  \ /
      |  NULL
      |
     \ /
     NULL

*/

/* the data for some upstream part to get updates from, some
 * some fields can be NULL or empty */
struct update_pattern {
	struct update_pattern *next;
	//e.g. "Name: woody"
	char *name;
	//e.g. "Method: ftp://ftp.uni-freiburg.de/pub/linux/debian"
	char *method;
	//e.g. "Config: Dir=/"
	char *config;
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
	//e.g. "UDebComponents: main>main"
	// (empty means all)
	struct strlist udebcomponents_from;
	struct strlist udebcomponents_into;
	// NULL is not allowed!:
	upgrade_decide_function *decide;
	void *decide_data;
};

struct update_origin {
	struct update_origin *next;
	const struct update_pattern *pattern;
	char *suite_from;
	const struct distribution *distribution;
	char *releasefile,*releasegpgfile;
	// is set when fetching packages..
	struct aptmethod *download;
	struct strlist checksums;
};

struct update_index {
	struct update_index *next;
	struct update_origin *origin;
	char *filename;
	char *upstream;
};

struct update_target {
	struct update_target *next;
	struct update_index *indices;
	struct target *target;
	struct upgradelist *upgradelist;
};

void update_pattern_free(struct update_pattern *update) {
	if( update == NULL )
		return;
	free(update->name);
	free(update->config);
	free(update->method);
	free(update->suite_from);
	free(update->verifyrelease);
	strlist_done(&update->architectures);
	strlist_done(&update->components_from);
	strlist_done(&update->components_into);
	strlist_done(&update->udebcomponents_from);
	strlist_done(&update->udebcomponents_into);
	free(update);
}

void updates_freepatterns(struct update_pattern *p) {
	while( p ) {
		struct update_pattern *pattern;

		pattern = p;
		p = pattern->next;
		update_pattern_free(pattern);
	}
}
void updates_freeorigins(struct update_origin *o) {
	while( o ) {
		struct update_origin *origin;

		origin = o;
		o = origin->next;
		free(origin->suite_from);
		free(origin->releasefile);
		free(origin->releasegpgfile);
		strlist_done(&origin->checksums);
		free(origin);
	}
}
void updates_freetargets(struct update_target *t) {
	while( t ) {
		struct update_target *ut;

		ut = t;
		t = ut->next;
		free(ut);
	}
}
static inline retvalue newupdatetarget(struct update_target **ts,struct target *target) {
	struct update_target *ut;

	ut = malloc(sizeof(struct update_target));
	if( ut == NULL )
		return RET_ERROR_OOM;
	ut->target = target;
	ut->next = *ts;
	ut->indices = NULL;
	ut->upgradelist = NULL;
	*ts = ut;
	return RET_OK;
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

inline static retvalue parse_pattern(const char *chunk, struct update_pattern **pattern) {
	struct update_pattern *update;
	struct strlist componentslist;
	retvalue r;

	update = calloc(1,sizeof(struct update_pattern));
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
		update_pattern_free(update);
		return r;
	}

	/* * Is there config for the method? * */
	r = chunk_getwholedata(chunk,"Config",&update->config);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	if( r == RET_NOTHING )
		update->config = NULL;

	/* * Check which suite to update from * */
	r = chunk_getvalue(chunk,"Suite",&update->suite_from);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	if( r == RET_NOTHING )
		update->suite_from = NULL;

	/* * Check which architectures to update from * */
	r = chunk_getwordlist(chunk,"Architectures",&update->architectures);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	if( r == RET_NOTHING ) {
		update->architectures.count = 0;
	}

	/* * Check which components to update from * */
	r = chunk_getwordlist(chunk,"Components",&componentslist);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
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
			update_pattern_free(update);
			return r;
		}
	}

	/* * Check which components to update udebs from * */
	r = chunk_getwordlist(chunk,"UDebComponents",&componentslist);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	if( r == RET_NOTHING ) {
		update->udebcomponents_from.count = 0;
		update->udebcomponents_into.count = 0;
	} else {
		r = splitcomponents(&update->udebcomponents_from,
				&update->udebcomponents_into,&componentslist);
		strlist_done(&componentslist);
		if( RET_WAS_ERROR(r) ) {
			update_pattern_free(update);
			return r;
		}
	}


	/* * Check if we should get the Release-file * */
	r = chunk_gettruth(chunk,"IgnoreRelease");
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	update->ignorerelease = (r == RET_OK);

	/* * Check if we should get the Release.gpg file * */
	r = chunk_getvalue(chunk,"VerifyRelease",&update->verifyrelease);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	if( r == RET_NOTHING )
		update->verifyrelease = NULL;

	update->decide = ud_always;
	update->decide_data = NULL;

	*pattern = update;
	return RET_OK;
}


static retvalue instance_pattern(const char *listdir,
		const struct update_pattern *pattern,
		const struct distribution *distribution,
		struct update_origin **origins) {

	struct update_origin *update;

	update = calloc(1,sizeof(struct update_origin));
	if( update == NULL )
		return RET_ERROR_OOM;

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
			free(update);
			return RET_ERROR;
		}
	}
	if( update->suite_from == NULL ) {
		free(update);
		return RET_ERROR_OOM;
	}

	update->distribution = distribution;
	update->pattern = pattern;

	if( !pattern->ignorerelease ) {
		update->releasefile = calc_downloadedlistfile(listdir,distribution->codename,pattern->name,"Release","data","rel");
		if( update->releasefile == NULL ) {
			updates_freeorigins(update);
			return RET_ERROR_OOM;
		}
		if( pattern->verifyrelease ) {
			update->releasegpgfile = calc_downloadedlistfile(listdir,distribution->codename,pattern->name,"Release","gpg","rel");
			if( update->releasegpgfile == NULL ) {
				updates_freeorigins(update);
				return RET_ERROR_OOM;
			}
		}
	}
	
	*origins = update;
	return RET_OK;
}

static retvalue parsechunk(void *data,const char *chunk) {
	struct update_pattern *update;
	struct update_pattern **patterns = data;
	retvalue r;

	r = parse_pattern(chunk,&update);
	if( RET_IS_OK(r) ) {
		update->next = *patterns;
		*patterns = update;
	}
	return r;
}

retvalue updates_getpatterns(const char *confdir,struct update_pattern **patterns,int force) {
	char *updatesfile;
	struct update_pattern *update = NULL;
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

static retvalue getorigins(const char *listdir,const struct update_pattern *patterns,const struct distribution *distribution,struct update_origin **origins) {
	const struct update_pattern *pattern;
	struct update_origin *updates = NULL;
	retvalue result;

	result = RET_NOTHING;

	for( pattern = patterns ; pattern ; pattern = pattern->next ) {
		struct update_origin *update;
		retvalue r;

		if( !strlist_in(&distribution->updates,pattern->name) )
			continue;
		r = instance_pattern(listdir,pattern,distribution,&update);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		if( RET_IS_OK(r) ) {
			update->next = updates;
			updates = update;
		}
	}
	if( RET_WAS_ERROR(result) ) {
		updates_freeorigins(updates);
	} else {
		*origins = updates;
	}
	return result;
}

static inline retvalue newindex(struct update_index **indices,
		const char *listdir,
		struct update_origin *origin,struct target *target,
		const char *component_from) {
	struct update_index *index;

	index = malloc(sizeof(struct update_index));
	if( index == NULL )
		return RET_ERROR_OOM;

	index->filename = calc_downloadedlistfile(listdir,
			target->codename,origin->pattern->name,
			component_from,
			target->architecture,target->suffix);
	if( index->filename == NULL ) {
		free(index);
		return RET_ERROR_OOM;
	}
	index->upstream = (*target->getupstreamindex)(target,
		origin->suite_from,component_from,target->architecture);
	if( index->upstream == NULL ) {
		free(index->filename);
		free(index);
		return RET_ERROR_OOM;
	}
	index->origin = origin;
	index->next = *indices;
	*indices = index;
	return RET_OK;
}

static retvalue gettargets(const char *listdir,struct update_origin *origins,struct distribution *distribution,struct update_target **ts) {
	struct target *target;
	struct update_origin *origin;
	struct update_target *updatetargets;
	const struct strlist *c_from,*c_into;
	retvalue r;
	int i;

	updatetargets = NULL;

	for( target = distribution->targets ; target ; target = target->next) {
		r = newupdatetarget(&updatetargets,target);
		if( RET_WAS_ERROR(r) ) {
			updates_freetargets(updatetargets);
			return r;
		}

		for( origin = origins ; origin ; origin=origin->next ) {
			const struct update_pattern *p = origin->pattern;

			if( p->architectures.count == 0 ) {
				if( !strlist_in(&origin->distribution->architectures,target->architecture) && strcmp(target->architecture,"source") != 0 )
					continue;
			} else {

				if( !strlist_in(&p->architectures,target->architecture))
					continue;
			}

			if( strcmp(target->suffix,"udeb") == 0 )  {
				if( p->udebcomponents_from.count > 0 ) {
					c_from = &p->udebcomponents_from;
					c_into = &p->udebcomponents_into;
				} else {
					c_from = &distribution->udebcomponents;
					c_into = &distribution->udebcomponents;
				}
			} else {
				if( p->components_from.count > 0 ) {
					c_from = &p->components_from;
					c_into = &p->components_into;
				} else {
					c_from = &distribution->components;
					c_into = &distribution->components;
				}
			}

			for( i = 0 ; i < c_into->count ; i++ ) {
				if( strcmp(c_into->values[i],
						target->component) == 0 ) { 
					r = newindex(&updatetargets->indices,
						listdir,origin,target,
						c_from->values[i]);

					if( RET_WAS_ERROR(r) ) {
						updates_freetargets(
							updatetargets);
						return r;
					}
				}
			}
		}
	}

	*ts = updatetargets;

	return RET_OK;
}

static inline retvalue findmissingupdate(int count,const struct distribution *distribution,struct update_origin *updates) {
	retvalue result;

	result = RET_OK;

	if( count != distribution->updates.count ) {
		int i;

		for( i=0;i<distribution->updates.count;i++ ){
			const char *update = distribution->updates.values[i];
			struct update_origin *u;

			u = updates;
			while( u && strcmp(u->pattern->name,update) != 0 ) 
				u = u->next;
			if( u == NULL ) {
				fprintf(stderr,"Update '%s' is listed in distribution '%s', but was not found!\n",update,distribution->codename);
				result = RET_ERROR_MISSING;
				break;
			}
		}
		if( RET_IS_OK(result) ) {
			fprintf(stderr,"Did you write an update two times in the update-line of '%s'?\n",distribution->codename);
			result = RET_NOTHING;
		}
	}

	return result;
}

retvalue updates_getindices(const char *listdir,const struct update_pattern *patterns,struct distribution *distributions) {
	struct distribution *distribution;

	for( distribution = distributions ; distribution ; distribution = distribution->next ) {
		struct update_origin *update;
		struct update_target *targets;
		retvalue r;

		r = getorigins(listdir,patterns,distribution,&update);

		if( RET_WAS_ERROR(r) )
			return r;
		if( RET_IS_OK(r) ) {
			struct update_origin *last;
			int count;

			assert(update);
			last = update;
			count = 1;
			while( last->next ) {
				last = last->next;
				count++;
			}
			/* Check if we got all: */
			r = findmissingupdate(count,distribution,update);
			if( RET_WAS_ERROR(r) ) {
				updates_freeorigins(update);
				return r;
			}

			r = gettargets(listdir,update,distribution,&targets);
			if( RET_WAS_ERROR(r) ) {
				updates_freeorigins(update);
				return r;
			}
			distribution->updateorigins = update;
			distribution->updatetargets = targets;
		}
	}
	return RET_OK;
}

/******************* Fetch all Lists for an update **********************/
static inline retvalue prepareorigin(struct aptmethodrun *run,struct update_origin *origin,struct distribution *distribution) {
	char *toget;
	struct aptmethod *method;
	retvalue r;
	const struct update_pattern *p = origin->pattern;

	r = aptmethod_newmethod(run,p->method,p->config,&method);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	origin->download = method;

	if( p->ignorerelease )
		return RET_NOTHING;

	toget = mprintf("dists/%s/Release",origin->suite_from);
	r = aptmethod_queueindexfile(method,toget,origin->releasefile);
	free(toget);
	if( RET_WAS_ERROR(r) )
		return r;

	if( p->verifyrelease != NULL ) {
		toget = mprintf("dists/%s/Release.gpg",origin->suite_from);
		r = aptmethod_queueindexfile(method,toget,origin->releasegpgfile);
		free(toget);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

static inline retvalue readchecksums(struct update_origin *origin) {
	retvalue r;

	if( origin->releasefile == NULL )
		return RET_NOTHING;

	/* if there is nothing said, then there is nothing to check... */
	if( origin->releasegpgfile != NULL ) {

		r = signature_check(origin->pattern->verifyrelease,
				origin->releasegpgfile,
				origin->releasefile);
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
	}
	r = release_getchecksums(origin->releasefile,&origin->checksums);
	assert( r != RET_NOTHING );
	return r;
}

static inline retvalue queueindex(struct update_index *index,int force) {
	const struct update_origin *origin = index->origin;
	int i;
	size_t l;

	if( origin->releasefile == NULL )
		return aptmethod_queueindexfile(origin->download,
			index->upstream,index->filename);

	//TODO: this is a very crude hack, think of something better...
	l = 7 + strlen(origin->suite_from); // == strlen("dists/%s/")

	for( i = 0 ; i+1 < origin->checksums.count ; i+=2 ) {

		assert( strlen(index->upstream) > l );
		if( strcmp(index->upstream+l,origin->checksums.values[i]) == 0 ){

			return aptmethod_queuefile(origin->download,
				index->upstream,index->filename,
				origin->checksums.values[i+1],NULL,NULL);
		}
		
	}
	fprintf(stderr,"Could not find '%s' within the Releasefile of '%s':\n'%s'\n",index->upstream,origin->pattern->name,origin->releasefile);
	if( force > 2 ) {
		aptmethod_queueindexfile(origin->download,
				index->upstream,index->filename);
	}
	return RET_ERROR_WRONG_MD5;
}



static retvalue updates_prepare(struct aptmethodrun *run,struct distribution *distribution) {
	retvalue result,r;
	struct update_origin *origin;

	result = RET_NOTHING;
	for( origin=distribution->updateorigins;origin; origin=origin->next ) {
		r = prepareorigin(run,origin,distribution);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return result;
}

static retvalue updates_queuelists(struct aptmethodrun *run,struct distribution *distribution,int force) {
	retvalue result,r;
	struct update_origin *origin;
	struct update_target *target;
	struct update_index *index;

	result = RET_NOTHING;
	for( origin=distribution->updateorigins;origin; origin=origin->next ) {
		r = readchecksums(origin);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && !force )
			return r;
	}
	for( target=distribution->updatetargets;target; target=target->next ) {
		for( index=target->indices ; index; index=index->next ) {
			r = queueindex(index,force);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && ! force )
				return r;
		}
	}
	return RET_OK;;
}

static inline retvalue searchformissing(const char *dbdir,struct downloadcache *cache,filesdb filesdb,struct update_target *u,upgrade_decide_function *decide,void *decision_data,int force) {
	struct update_index *index;
	retvalue result,r;

	if( verbose > 2 )
		fprintf(stderr,"  processing updates for '%s'\n",u->target->identifier);
	r = upgradelist_initialize(&u->upgradelist,u->target,dbdir,decide,decision_data);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;

	for( index=u->indices ; index ; index=index->next ) {

		if( verbose > 4 )
			fprintf(stderr,"  reading '%s'\n",index->filename);
		assert(index->origin->download);
		r = upgradelist_update(u->upgradelist,
				index->origin->download,index->filename,
				index->origin->pattern->decide,
				index->origin->pattern->decide_data,
				force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && !force)
			return result;
	}
	//TODO: when upgradelist supports removing unavail packages,
	//do not forget to disable this here in case of error and force...
	
	r = upgradelist_enqueue(u->upgradelist,cache,filesdb,force);
	RET_UPDATE(result,r);

	return result;
}

static retvalue updates_readindices(const char *dbdir,struct downloadcache *cache,filesdb filesdb,struct distribution *distribution,int force) {
	retvalue result,r;
	struct update_target *u;

	result = RET_NOTHING;
	for( u=distribution->updatetargets ; u ; u=u->next ) {
		r = searchformissing(dbdir,cache,filesdb,u,ud_always,NULL,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && !force )
			break;
	}
	return result;
}


static retvalue updates_install(const char *dbdir,filesdb filesdb,DB *refsdb,struct distribution *distribution,int force) {
	retvalue result,r;
	struct update_target *u;

	result = RET_NOTHING;
	for( u=distribution->updatetargets ; u ; u=u->next ) {
		r = upgradelist_install(u->upgradelist,dbdir,filesdb,refsdb,force);
		RET_UPDATE(result,r);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
		if( RET_WAS_ERROR(r) && !force )
			break;
	}
	return result;
}

retvalue updates_update(const char *dbdir,const char *listdir,const char *methoddir,filesdb filesdb,DB *refsdb,struct distribution *distributions,int force) {
	struct distribution *distribution;
	retvalue result,r;
	struct aptmethodrun *run;
	struct downloadcache *cache;

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;
	/* first get all "Release" and "Release.gpg" files */
	for( distribution=distributions ; distribution ; distribution=distribution->next) {
		if( distribution->override || distribution->srcoverride ) {
			if( verbose >= 0 )
				fprintf(stderr,"Warning: Override-Files of '%s' ignored as not yet supported while updating!\n",distribution->codename);
		}
		r = updates_prepare(run,distribution);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force )
			break;
	}
	if( RET_WAS_ERROR(result) && !force ) {
		aptmethod_shutdown(run);
		return result;
	}

	r = aptmethod_download(run,methoddir,filesdb);
	if( RET_WAS_ERROR(r) && !force ) {
		RET_UPDATE(result,r);
		aptmethod_shutdown(run);
		return result;
	}

	/* Then get all index files (with perhaps md5sums from the above) */
	for( distribution=distributions ; distribution ; distribution=distribution->next) {
		r = updates_queuelists(run,distribution,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force )
			break;
	}
	if( RET_WAS_ERROR(result) && !force ) {
		RET_UPDATE(result,r);
		aptmethod_shutdown(run);
		return result;
	}

	r = aptmethod_download(run,methoddir,filesdb);
	if( RET_WAS_ERROR(r) && !force ) {
		RET_UPDATE(result,r);
		aptmethod_shutdown(run);
		return result;
	}

	/* Then get all packages */
	if( verbose >= 0 )
		fprintf(stderr,"Calculating packages to get...\n");
	r = downloadcache_initialize(&cache);
	if( !RET_IS_OK(r) ) {
		aptmethod_shutdown(run);
		RET_UPDATE(result,r);
		return result;
	}
	
	for( distribution=distributions ; distribution ; distribution=distribution->next) {
		r = updates_readindices(dbdir,cache,filesdb,distribution,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force )
			break;
	}
	if( verbose >= 0 )
		fprintf(stderr,"Getting packages...\n");
	r = aptmethod_download(run,methoddir,filesdb);
	RET_UPDATE(result,r);
	if( verbose > 0 )
		fprintf(stderr,"Freeing some memory...\n");
	r = downloadcache_free(cache);
	RET_UPDATE(result,r);
	if( verbose > 0 )
		fprintf(stderr,"Shutting down aptmethods...\n");
	r = aptmethod_shutdown(run);
	RET_UPDATE(result,r);

	if( RET_WAS_ERROR(result) && !force ) {
		return result;
	}
	if( verbose >= 0 )
		fprintf(stderr,"Installing packages...\n");

	for( distribution=distributions ; distribution ; distribution=distribution->next) {
		r = updates_install(dbdir,filesdb,refsdb,distribution,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force )
			break;
	}

	return result;
}
