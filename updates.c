/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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
#include "terms.h"

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
	bool_t ignorerelease;
	//e.g. "ReleaseCheck: B629A24C38C6029A" (NULL means not check)
	char *verifyrelease;
	//e.g. "Architectures: i386 sparc mips" (empty means all)
	struct strlist architectures_from;
	struct strlist architectures_into;
	//e.g. "Components: main>main non-free>non-free contrib>contrib"
	// (empty means all)
	struct strlist components_from;
	struct strlist components_into;
	//e.g. "UDebComponents: main>main"
	// (empty means all)
	struct strlist udebcomponents_from;
	struct strlist udebcomponents_into;
	// NULL means no condition
	term *includecondition;
};

struct update_origin {
	struct update_origin *next;
	/* all following are NULL when this is a delete rule */
	const struct update_pattern *pattern;
	char *suite_from;
	const struct distribution *distribution;
	char *releasefile,*releasegpgfile;
	/* set when there was a error and it should no loner be used */
	bool_t failed;
	// is set when fetching packages..
	struct aptmethod *download;
	struct strlist checksums;
};

struct update_index {
	struct update_index *next;
	/* all following are NULL when this is a delete rule */
	struct update_origin *origin;
	char *filename;
	char *upstream;
	/* there was something missed here */
	bool_t failed;
};

struct update_target {
	struct update_target *next;
	struct update_index *indices;
	struct target *target;
	struct upgradelist *upgradelist;
	/* Ignore delete marks (as some lists were missing) */
	bool_t ignoredelete;
};

void update_pattern_free(struct update_pattern *update) {
	if( update == NULL )
		return;
	free(update->name);
	free(update->config);
	free(update->method);
	free(update->suite_from);
	free(update->verifyrelease);
	strlist_done(&update->architectures_from);
	strlist_done(&update->architectures_into);
	strlist_done(&update->components_from);
	strlist_done(&update->components_into);
	strlist_done(&update->udebcomponents_from);
	strlist_done(&update->udebcomponents_into);
	term_free(update->includecondition);
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
	ut->ignoredelete = FALSE;
	*ts = ut;
	return RET_OK;
}

static retvalue splitlist(struct strlist *from,
				struct strlist *into,
				const struct strlist *list) {
	retvalue r;
	int i;

	r = strlist_init(from);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	r = strlist_init(into);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(from);
		return r;
	}

	/* * Iterator over components to update * */
	r = RET_NOTHING;
	for( i = 0 ; i < list->count ; i++ ) {
		const char *item,*seperator;
		char *origin,*destination;

		item = list->values[i];
		// TODO: isn't this broken for the !*(dest+1) case ?
		if( !(seperator = strchr(item,'>')) ) {
			destination = strdup(item);
			origin = strdup(item);
		} else if( seperator == item ) {
			destination = strdup(seperator+1);
			origin = strdup(seperator+1);
		} else if( *(seperator+1) == '\0' ) {
			destination = strndup(item,seperator-item);
			origin = strndup(item,seperator-item);
		} else {
			origin = strndup(item,seperator-item);
			destination = strdup(seperator+1);
		}
		if( !origin || ! destination ) {
			free(origin);free(destination);
			strlist_done(from);
			strlist_done(into);
			return RET_ERROR_OOM;
		}
		r = strlist_add(from,origin);
		if( RET_WAS_ERROR(r) ) {
			free(destination);
			strlist_done(from);
			strlist_done(into);
			return r;
		}
		r = strlist_add(into,destination);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(from);
			strlist_done(into);
			return r;
		}
		r = RET_OK;
	}
	return r;
}

inline static retvalue parse_pattern(const char *chunk, struct update_pattern **pattern) {
	struct update_pattern *update;
	struct strlist componentslist,architectureslist;
	char *formula;
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
	r = chunk_getwordlist(chunk,"Architectures",&architectureslist);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	if( r == RET_NOTHING ) {
		update->architectures_from.count = 0;
		update->architectures_into.count = 0;
	} else {
		r = splitlist(&update->architectures_from,
				&update->architectures_into,&architectureslist);
		strlist_done(&architectureslist);
		if( RET_WAS_ERROR(r) ) {
			update_pattern_free(update);
			return r;
		}
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
		r = splitlist(&update->components_from,
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
		r = splitlist(&update->udebcomponents_from,
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

	/* * Check if there is a Include condition * */
	r = chunk_getvalue(chunk,"FilterFormula",&formula);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	if( r != RET_NOTHING ) {
		r = term_compile(&update->includecondition,formula,
			T_OR|T_BRACKETS|T_NEGATION|T_VERSION|T_NOTEQUAL);
		free(formula);
		if( RET_WAS_ERROR(r) ) {
			update->includecondition = NULL;
			update_pattern_free(update);
			return r;
		}
		assert( r != RET_NOTHING );
	}

	*pattern = update;
	return RET_OK;
}

static retvalue new_deleterule(struct update_origin **origins) {

	struct update_origin *update;

	update = calloc(1,sizeof(struct update_origin));
	if( update == NULL )
		return RET_ERROR_OOM;

	*origins = update;
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
	update->failed = FALSE;
	update->download = NULL;

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
	r = chunk_foreach(updatesfile,parsechunk,&update,force,FALSE);
	free(updatesfile);
	if( RET_IS_OK(r) )
		*patterns = update;
	return r;
}

static retvalue getorigins(const char *listdir,const struct update_pattern *patterns,const struct distribution *distribution,struct update_origin **origins) {
	struct update_origin *updates = NULL;
	retvalue result;
	int i;

	result = RET_NOTHING;
	for( i = 0; i < distribution->updates.count ; i++ ) {
		const char *name = distribution->updates.values[i];
		const struct update_pattern *pattern;
		struct update_origin *update;
		retvalue r;

		if( strcmp(name,"-") == 0 ) {
			r = new_deleterule(&update);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
			if( RET_IS_OK(r) ) {
				update->next = updates;
				updates = update;
			}
			continue;
		}

		for( pattern = patterns ; pattern ; pattern = pattern->next ) {
			if( strcmp(name,pattern->name) == 0 )
				break;
		}
		if( pattern == NULL ) {
			fprintf(stderr,"Cannot find definition of upgrade-rule '%s' for distribution '%s'!\n",name,distribution->codename);
			RET_UPDATE(result,RET_ERROR);
			break;
		}

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
		const char *architecture_from,
		const char *component_from) {
	struct update_index *index;

	index = malloc(sizeof(struct update_index));
	if( index == NULL )
		return RET_ERROR_OOM;

	assert( origin != NULL && origin->pattern != NULL);

	index->filename = calc_downloadedlistfile(listdir,
			target->codename,origin->pattern->name,
			component_from,architecture_from,
			target->packagetype);
	if( index->filename == NULL ) {
		free(index);
		return RET_ERROR_OOM;
	}
	index->upstream = (*target->getupstreamindex)(target,
		origin->suite_from,component_from,architecture_from);
	if( index->upstream == NULL ) {
		free(index->filename);
		free(index);
		return RET_ERROR_OOM;
	}
	index->origin = origin;
	index->next = *indices;
	index->failed = FALSE;
	*indices = index;
	return RET_OK;
}


static retvalue addorigintotarget(const char *listdir,struct update_origin *origin,
		struct target *target,
		struct distribution *distribution,
		struct update_target *updatetargets ) {
	const struct update_pattern *p = origin->pattern;
	const struct strlist *c_from,*c_into;
	const struct strlist *a_from,*a_into;
	int ai,ci;
	retvalue r;

	assert( origin != NULL && origin->pattern != NULL);

	if( p->architectures_into.count == 0 ) {
		a_from = &distribution->architectures;
		a_into = &distribution->architectures;
	} else {
		a_from = &p->architectures_from;
		a_into = &p->architectures_into;
	}
	if( strcmp(target->packagetype,"udeb") == 0 )  {
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

	for( ai = 0 ; ai < a_into->count ; ai++ ) {
		if( strcmp(a_into->values[ai],target->architecture) != 0 )
			continue;

		for( ci = 0 ; ci < c_into->count ; ci++ ) {
			if( strcmp(c_into->values[ci],target->component) != 0 )
				continue;

			r = newindex(&updatetargets->indices,listdir,origin,target,
					a_from->values[ai],c_from->values[ci]);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return RET_OK;
}

static retvalue adddeleteruletotarget(struct update_target *updatetargets ) {
	struct update_index *index;

	index = calloc(1,sizeof(struct update_index));
	if( index == NULL )
		return RET_ERROR_OOM;
	index->next = updatetargets->indices;
	updatetargets->indices = index;
	return RET_OK;
}

static retvalue gettargets(const char *listdir,struct update_origin *origins,struct distribution *distribution,struct update_target **ts) {
	struct target *target;
	struct update_origin *origin;
	struct update_target *updatetargets;
	retvalue r;

	updatetargets = NULL;

	for( target = distribution->targets ; target ; target = target->next) {
		r = newupdatetarget(&updatetargets,target);
		if( RET_WAS_ERROR(r) ) {
			updates_freetargets(updatetargets);
			return r;
		}

		for( origin = origins ; origin ; origin=origin->next ) {
			if( origin->pattern == NULL )
				r = adddeleteruletotarget(updatetargets);
			else
				r = addorigintotarget(listdir,origin,target,
						distribution,updatetargets);
			if( RET_WAS_ERROR(r) ) {
				updates_freetargets(updatetargets);
				return r;
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

retvalue updates_calcindices(const char *listdir,const struct update_pattern *patterns,struct distribution *distributions) {
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

/************************* Preparations *********************************/
static inline retvalue startuporigin(struct aptmethodrun *run,struct update_origin *origin,struct distribution *distribution) {
	retvalue r;
	struct aptmethod *method;

	assert( origin != NULL && origin->pattern != NULL );
	r = aptmethod_newmethod(run,origin->pattern->method,
			origin->pattern->config,&method);
	if( RET_WAS_ERROR(r) ) {
		origin->download = NULL;
		origin->failed = TRUE;
		return r;
	}
	origin->download = method;
	return RET_OK;
}

static retvalue updates_startup(struct aptmethodrun *run,struct distribution *distributions, int force) {
	retvalue result,r;
	struct update_origin *origin;
	struct distribution *distribution;

	result = RET_NOTHING;
	for( distribution=distributions ; distribution ; distribution=distribution->next) {
		if( distribution->override || distribution->srcoverride ) {
			if( verbose >= 0 )
				fprintf(stderr,"Warning: Override-Files of '%s' ignored as not yet supported while updating!\n",distribution->codename);
		}
		for( origin=distribution->updateorigins; origin; origin=origin->next ) {
			if( origin->pattern == NULL)
				continue;
			r = startuporigin(run,origin,distribution);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				return r;
		}
	}
	return result;
}
/******************* Fetch all Lists for an update **********************/
static inline retvalue queuemetalists(struct aptmethodrun *run,struct update_origin *origin,struct distribution *distribution) {
	char *toget;
	retvalue r;
	const struct update_pattern *p = origin->pattern;

	assert( origin != NULL && origin->pattern != NULL );

	if( origin->download == NULL ) {
		fprintf(stderr,"Cannot download '%s' as no method started!\n",origin->releasefile);
		return RET_ERROR;
	}

	toget = mprintf("dists/%s/Release",origin->suite_from);
	r = aptmethod_queueindexfile(origin->download,
			toget,origin->releasefile);
	free(toget);
	if( RET_WAS_ERROR(r) )
		return r;

	if( p->verifyrelease != NULL ) {
		toget = mprintf("dists/%s/Release.gpg",origin->suite_from);
		r = aptmethod_queueindexfile(origin->download,
				toget,origin->releasegpgfile);
		free(toget);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

static inline retvalue readchecksums(struct update_origin *origin) {
	retvalue r;

	assert( origin != NULL && origin->pattern != NULL );

	if( origin->releasefile == NULL )
		return RET_NOTHING;
	if( origin->failed )
		return RET_NOTHING;

	/* if there is nothing said, then there is nothing to check... */
	if( origin->releasegpgfile != NULL ) {

		r = signature_check(origin->pattern->verifyrelease,
				origin->releasegpgfile,
				origin->releasefile);
		if( RET_WAS_ERROR(r) ) {
			origin->failed = TRUE;
			return r;
		}
	}
	r = release_getchecksums(origin->releasefile,&origin->checksums);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		origin->failed = TRUE;
	return r;
}

static inline retvalue queueindex(struct update_index *index,int force) {
	const struct update_origin *origin = index->origin;
	int i;
	size_t l;

	assert( index != NULL && index->origin != NULL );
	if( origin->download == NULL || origin->failed ) {
		return RET_NOTHING;
	}
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



static retvalue updates_queuemetalists(struct aptmethodrun *run,struct distribution *distributions, int force) {
	retvalue result,r;
	struct update_origin *origin;
	struct distribution *distribution;

	result = RET_NOTHING;
	for( distribution=distributions ; distribution ; distribution=distribution->next) {
		for( origin=distribution->updateorigins;origin; origin=origin->next ) {
			if( origin->pattern == NULL)
				continue;
			if( origin->pattern->ignorerelease )
				continue;
			r = queuemetalists(run,origin,distribution);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				return r;
		}

	}

	return result;
}

static retvalue updates_queuelists(struct aptmethodrun *run,struct distribution *distributions,int force) {
	retvalue result,r;
	struct update_origin *origin;
	struct update_target *target;
	struct update_index *index;
	struct distribution *distribution;

	result = RET_NOTHING;
	for( distribution=distributions ; distribution ; distribution=distribution->next) {

		for( origin=distribution->updateorigins;origin; origin=origin->next ) {
			if( origin->pattern == NULL)
				continue;
			r = readchecksums(origin);
			RET_UPDATE(result,r);
			//TODO: more force needed?
			if( RET_WAS_ERROR(r) && !force )
				return r;
		}
		for( target=distribution->updatetargets;target; target=target->next ) {
			for( index=target->indices ; index; index=index->next ) {
				if( index->origin == NULL )
					continue;
				r = queueindex(index,force);
				RET_UPDATE(result,r);
				if( RET_WAS_ERROR(r) && ! force )
					return r;
			}
		}
	}
	return result;
}


upgrade_decision ud_decide_by_pattern(void *privdata, const char *package,const char *old_version,const char *new_version,const char *newcontrolchunk) {
	struct update_pattern *pattern = privdata;

	if( pattern->includecondition ) {
		struct term_atom *atom = pattern->includecondition;
		while( atom ) {
			bool_t correct;char *value;
			enum term_comparison c = atom->comparison;
			retvalue r;

			r = chunk_getvalue(newcontrolchunk,atom->key,&value);
			// gna..., why is there no way to report errors?
			// TODO: fix this insanity...
			if( RET_WAS_ERROR(r) )
				r = RET_NOTHING;
			if( r == RET_NOTHING ) {
//				fprintf(stderr,"not found %s\n",atom->key);
				correct = ( c != tc_notequal );
			} else if( c == tc_none) {
				correct = TRUE;
				free(value);
			} else {
				int i;
//				fprintf(stderr,"found %s as '%s' (will compare with '%s')\n",atom->key,value,atom->comparewith);
				i = strcmp(value,atom->comparewith);
				free(value);
				if( i < 0 ) 
					correct = c == tc_strictless
					     || c == tc_lessorequal
					     || c == tc_notequal;
				else if( i > 0 ) 
					correct = c == tc_strictmore
					     || c == tc_moreorequal
					     || c == tc_notequal;
				else 
					correct = c == tc_lessorequal
					     || c == tc_moreorequal
					     || c == tc_equal;
			}
			if( atom->negated )
				correct = !correct;
			if( correct ) {
				atom = atom->nextiftrue;
			} else {
				atom = atom->nextiffalse;
				if( atom == NULL) {
					// fprintf(stderr,"Rejecting %s\n",package);
					return UD_NO;
				}
			}

		}
	}
	// fprintf(stderr,"Accepting %s\n",package);

	return UD_UPGRADE;
}

static inline retvalue searchformissing(const char *dbdir,struct update_target *u,upgrade_decide_function *decide,void *decision_data,int force) {
	struct update_index *index;
	retvalue result,r;

	if( verbose > 2 )
		fprintf(stderr,"  processing updates for '%s'\n",u->target->identifier);
	r = upgradelist_initialize(&u->upgradelist,u->target,dbdir,decide,decision_data);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;

	for( index=u->indices ; index ; index=index->next ) {

		if( index->origin == NULL ) {
			if( verbose > 4 )
				fprintf(stderr,"  marking everything to be deleted\n");
			r = upgradelist_deleteall(u->upgradelist);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && !force)
				return result;
			u->ignoredelete = FALSE;
			continue;
		}

		if( index->failed || index->origin->failed ) {
			if( verbose >= 1 )
				fprintf(stderr,"  missing '%s'\n",index->filename);
			u->ignoredelete = TRUE;
			continue;
		}

		if( verbose > 4 )
			fprintf(stderr,"  reading '%s'\n",index->filename);
		assert(index->origin->download);
		r = upgradelist_update(u->upgradelist,
				index->origin->download,index->filename,
				ud_decide_by_pattern,
				(void*)index->origin->pattern,
				force);
		if( RET_WAS_ERROR(r) )
			u->ignoredelete = TRUE;
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && !force)
			return result;
	}
	
	return result;
}

static retvalue updates_readindices(const char *dbdir,struct distribution *distribution,int force) {
	retvalue result,r;
	struct update_target *u;

	result = RET_NOTHING;
	for( u=distribution->updatetargets ; u ; u=u->next ) {
		r = searchformissing(dbdir,u,ud_always,NULL,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && !force )
			break;
	}
	return result;
}

static retvalue updates_enqueue(const char *dbdir,struct downloadcache *cache,filesdb filesdb,struct distribution *distribution,int force) {
	retvalue result,r;
	struct update_target *u;

	result = RET_NOTHING;
	for( u=distribution->updatetargets ; u ; u=u->next ) {
		r = upgradelist_enqueue(u->upgradelist,cache,filesdb,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && !force )
			break;
	}
	return result;
}


static retvalue updates_install(const char *dbdir,filesdb filesdb,references refs,struct distribution *distribution,int force,struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	struct update_target *u;

	result = RET_NOTHING;
	for( u=distribution->updatetargets ; u ; u=u->next ) {
		r = upgradelist_install(u->upgradelist,dbdir,filesdb,refs,force,u->ignoredelete,dereferencedfilekeys);
		RET_UPDATE(result,r);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
		if( RET_WAS_ERROR(r) && !force )
			break;
	}
	return result;
}

static void updates_dump(struct distribution *distribution) {
	struct update_target *u;

	for( u=distribution->updatetargets ; u ; u=u->next ) {
		printf("Updates needed for '%s':\n",u->target->identifier);
		upgradelist_dump(u->upgradelist);
	}
}

static retvalue updates_downloadlists(const char *methoddir,struct aptmethodrun *run,struct distribution *distributions, int force) {
	retvalue r,result;

	/* first get all "Release" and "Release.gpg" files */
	result = updates_queuemetalists(run,distributions,force);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		return result;
	}

	r = aptmethod_download(run,methoddir,NULL);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(r) && !force ) {
		return result;
	}

	/* Then get all index files (with perhaps md5sums from the above) */
	r = updates_queuelists(run,distributions,force);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(result) && !force ) {
		return result;
	}

	r = aptmethod_download(run,methoddir,NULL);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(r) && !force ) {
		return result;
	}
	return result;
}

retvalue updates_update(const char *dbdir,const char *methoddir,filesdb filesdb,references refs,struct distribution *distributions,int force,bool_t nolistdownload,struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	struct distribution *distribution;
	struct aptmethodrun *run;
	struct downloadcache *cache;

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) )
		return r;

	/* preperations */
	result = updates_startup(run,distributions,force);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		aptmethod_shutdown(run);
		return result;
	}
	if( nolistdownload ) {
		if( verbose >= 0 )
			fprintf(stderr,"Warning: As --nolistsdownload is given, index files are NOT checked.\n");
	} else {
		r = updates_downloadlists(methoddir,run,distributions,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(result) && force <= 0 ) {
			aptmethod_shutdown(run);
			return result;
		}

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
		r = updates_readindices(dbdir,distribution,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force )
			break;
		r = updates_enqueue(dbdir,cache,filesdb,distribution,force);
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
		fprintf(stderr,"Installing (and possibly deleting) packages...\n");

	for( distribution=distributions ; distribution ; distribution=distribution->next) {
		r = updates_install(dbdir,filesdb,refs,distribution,force,dereferencedfilekeys);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force )
			break;
	}

	return result;
}

retvalue updates_checkupdate(const char *dbdir,const char *methoddir,struct distribution *distributions,int force,bool_t nolistdownload) {
	struct distribution *distribution;
	retvalue result,r;
	struct aptmethodrun *run;

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) )
		return r;

	result = updates_startup(run,distributions,force);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		aptmethod_shutdown(run);
		return result;
	}
	if( nolistdownload ) {
		if( verbose >= 0 )
			fprintf(stderr,"Warning: As --nolistsdownload is given, index files are NOT checked.\n");
	} else {
		r = updates_downloadlists(methoddir,run,distributions,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(result) && force <= 0 ) {
			aptmethod_shutdown(run);
			return result;
		}
	}
	if( verbose > 0 )
		fprintf(stderr,"Shutting down aptmethods...\n");
	r = aptmethod_shutdown(run);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(result) && !force ) {
		return result;
	}

	/* Then look what packages to get */
	if( verbose >= 0 )
		fprintf(stderr,"Calculating packages to get...\n");
	
	for( distribution=distributions ; distribution ; distribution=distribution->next) {
		r = updates_readindices(dbdir,distribution,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force )
			break;
		updates_dump(distribution);
	}

	return result;
}
