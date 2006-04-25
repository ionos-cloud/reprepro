/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include <config.h>

#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
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
#include "md5sum.h"
#include "aptmethod.h"
#include "updates.h"
#include "upgradelist.h"
#include "distribution.h"
#include "terms.h"
#include "filterlist.h"
#include "readrelease.h"
#include "donefile.h"

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
	//e.g. "Fallback: ftp://ftp.debian.org/pub/linux/debian"
	char *fallback; // can be other server or dir, but must be same method
	//e.g. "Config: Dir=/"
	/*@null@*/char *config;
	//e.g. "Suite: woody" or "Suite: <asterix>/updates" (NULL means "*")
	/*@null@*/char *suite_from;
	//e.g. "IgnoreRelease: Yes" for 1 (default is 0)
	bool_t ignorerelease;
	//e.g. "VerifyRelease: B629A24C38C6029A" (NULL means not check)
	/*@null@*/char *verifyrelease;
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
	/*@null@*/term *includecondition;
	struct filterlist filterlist;
	// NULL means nothing to execute after lists are downloaded...
	/*@null@*/char *listhook;
};

struct update_origin {
	struct update_origin *next;
	/* all following are NULL when this is a delete rule */
	/*@null@*/const struct update_pattern *pattern;
	/*@null@*/char *suite_from;
	/*@null@*/const struct distribution *distribution;
	/*@null@*/char *releasefile,*releasegpgfile;
	/* set when there was a error and it should no loner be used */
	bool_t failed;
	// is set when fetching packages..
	/*@null@*/struct aptmethod *download;
	/*@null@*/struct strlist checksums;
};

struct update_index {
	struct update_index *next;
	/* all following are NULL when this is a delete rule */
	struct update_origin *origin;
	char *filename;
	char *upstream;
	/* != NULL if filename was changed by ListHook, then the original */
	char *original_filename;
	/* index into origin's checkfile, -1 = none */
	int checksum_ofs;
	/* there was something missed here */
	bool_t failed;
};

struct update_target {
	/*@null@*/struct update_target *next;
	/*@null@*/struct update_index *indices;
	/*@dependent@*/struct target *target;
	/*@null@*/struct upgradelist *upgradelist;
	/* Ignore delete marks (as some lists were missing) */
	bool_t ignoredelete;
	/* don't do anything because of --skipold */
	bool_t nothingnew;
	/* if true do not generate donefiles */
	bool_t incomplete;
};

struct update_distribution {
	struct update_distribution *next;
	struct distribution *distribution;
	struct update_origin *origins;
	struct update_target *targets;
};

static void update_pattern_free(/*@only@*/struct update_pattern *update) {
	if( update == NULL )
		return;
	free(update->name);
	free(update->config);
	free(update->method);
	free(update->fallback);
	free(update->suite_from);
	free(update->verifyrelease);
	strlist_done(&update->architectures_from);
	strlist_done(&update->architectures_into);
	strlist_done(&update->components_from);
	strlist_done(&update->components_into);
	strlist_done(&update->udebcomponents_from);
	strlist_done(&update->udebcomponents_into);
	term_free(update->includecondition);
	filterlist_release(&update->filterlist);
	free(update->listhook);
	free(update);
}

void updates_freepatterns(struct update_pattern *p) {
	while( p != NULL ) {
		struct update_pattern *pattern;

		pattern = p;
		p = pattern->next;
		update_pattern_free(pattern);
	}
}

static void updates_freeorigins(/*@only@*/struct update_origin *o) {
	while( o != NULL ) {
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

static void updates_freetargets(/*@only@*/struct update_target *t) {
	while( t != NULL ) {
		struct update_target *ut;

		ut = t;
		t = ut->next;
		while( ut->indices != NULL ) {
			struct update_index *ui;

			ui = ut->indices;
			ut->indices = ui->next;
			free(ui->filename);
			free(ui->original_filename);
			free(ui->upstream);
			free(ui);
		}
		free(ut);
	}
}

void updates_freeupdatedistributions(struct update_distribution *d) {
	while( d != NULL ) {
		struct update_distribution *next;

		next = d->next;
		updates_freetargets(d->targets);
		updates_freeorigins(d->origins);
		free(d);
		d = next;
	}
}

static inline retvalue newupdatetarget(struct update_target **ts,/*@dependent@*/struct target *target) {
	struct update_target *ut;

	ut = malloc(sizeof(struct update_target));
	if( ut == NULL )
		return RET_ERROR_OOM;
	ut->target = target;
	ut->next = *ts;
	ut->indices = NULL;
	ut->upgradelist = NULL;
	ut->ignoredelete = FALSE;
	ut->nothingnew = FALSE;
	ut->incomplete = FALSE;
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
		if( (seperator = strchr(item,'>')) == NULL ) {
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
		if( origin == NULL || destination == NULL ) {
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

inline static retvalue parse_pattern(const char *confdir,const char *chunk, struct update_pattern **pattern) {
	struct update_pattern *update;
	struct strlist componentslist,architectureslist;
	char *formula,*filename;
	retvalue r;
	static const char * const allowedfields[] = {"Name", "Method", 
"Config", "Suite", "Architectures", "Components", "UDebComponents", 
"IgnoreRelease", "VerifyRelease", "FilterFormula", "FilterList",
"ListHook", "Fallback", NULL};

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

	/* * Look for alternate server: * */
	r = chunk_getvalue(chunk,"Fallback",&update->fallback);
	if( r == RET_NOTHING )
		update->fallback = NULL;
	else if( !RET_IS_OK(r) ) {
		update_pattern_free(update);
		return r;
	}

	// TODO: if those are checked anyway, there should be no reason to
	// research them later...
	r = chunk_checkfields(chunk,allowedfields,TRUE);
	if( RET_WAS_ERROR(r) ) {
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
	/* * Check if there is a list to say what can be included by update * */
	r = chunk_getvalue(chunk,"FilterList",&filename);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	}
	if( r != RET_NOTHING ) {
		r = filterlist_load(&update->filterlist,confdir,filename);
		free(filename);
		if( RET_WAS_ERROR(r) ) {
			update_pattern_free(update);
			return r;
		}
		assert( r != RET_NOTHING );
	} else {
		filterlist_empty(&update->filterlist,flt_install);
	}
	/* * Check if there is a script to call * */
	r = chunk_getvalue(chunk,"ListHook",&update->listhook);
	if( RET_WAS_ERROR(r) ) {
		update_pattern_free(update);
		return r;
	} else if( r == RET_NOTHING ) {
		update->listhook = NULL;
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
		else if( strchr(pattern->suite_from,'*') == NULL )
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
		if( pattern->verifyrelease != NULL ) {
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

struct getpatterns_data {
	struct update_pattern **patterns;
	const char *confdir;
};

static retvalue parsechunk(void *data,const char *chunk) {
	struct update_pattern *update;
	struct getpatterns_data *d = data;
	retvalue r;

	r = parse_pattern(d->confdir,chunk,&update);
	if( RET_IS_OK(r) ) {
		update->next = *d->patterns;
		*d->patterns = update;
	}
	return r;
}

retvalue updates_getpatterns(const char *confdir,struct update_pattern **patterns) {
	char *updatesfile;
	struct update_pattern *update = NULL;
	struct getpatterns_data data;
	retvalue r;

	updatesfile = calc_dirconcat(confdir,"updates");
	if( updatesfile == NULL ) 
		return RET_ERROR_OOM;
	data.patterns = &update;
	data.confdir = confdir;
	r = chunk_foreach(updatesfile,parsechunk,&data,0,FALSE);
	free(updatesfile);
	if( RET_IS_OK(r) )
		*patterns = update;
	else if( r == RET_NOTHING ) {
		*patterns = NULL;
		r = RET_OK;
	}
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

		for( pattern = patterns ; pattern != NULL ; pattern = pattern->next ) {
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
	index->original_filename = NULL;
	index->upstream = (*target->getupstreamindex)(target,
		origin->suite_from,component_from,architecture_from);
	if( index->upstream == NULL ) {
		free(index->filename);
		free(index);
		return RET_ERROR_OOM;
	}
	index->origin = origin;
	index->checksum_ofs = -1;
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

	for( target = distribution->targets ; target != NULL ; target = target->next) {
		r = newupdatetarget(&updatetargets,target);
		if( RET_WAS_ERROR(r) ) {
			updates_freetargets(updatetargets);
			return r;
		}

		for( origin = origins ; origin != NULL ; origin=origin->next ) {
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
			while( u != NULL && strcmp(u->pattern->name,update) != 0 ) 
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

retvalue updates_calcindices(const char *listdir,const struct update_pattern *patterns,struct distribution *distributions,struct update_distribution **update_distributions) {
	struct distribution *distribution;
	struct update_distribution *u_ds;
	retvalue r;

	u_ds = NULL;
	r = RET_OK;

	for( distribution = distributions ; distribution != NULL ; 
			       distribution = distribution->next ) {
		struct update_distribution *u_d;

		u_d = calloc(1,sizeof(struct update_distribution));
		if( u_d == NULL ) {
			r = RET_ERROR_OOM;
			break;
		}
		u_d->distribution = distribution;
		u_d->next = u_ds;
		u_ds = u_d;

		r = getorigins(listdir,patterns,distribution,&u_d->origins);

		if( RET_WAS_ERROR(r) )
			break;
		if( RET_IS_OK(r) ) {
			struct update_origin *last;
			int count;

			assert( u_d->origins != NULL );
			last = u_d->origins;
			count = 1;
			while( last->next != NULL ) {
				last = last->next;
				count++;
			}
			/* Check if we got all: */
			r = findmissingupdate(count,distribution,u_d->origins);
			if( RET_WAS_ERROR(r) )
				break;

			r = gettargets(listdir,u_d->origins,distribution,&u_d->targets);
			if( RET_WAS_ERROR(r) )
				break;
		}
		r = RET_OK;
	}
	if( RET_IS_OK(r) )
		*update_distributions = u_ds;
	else
		updates_freeupdatedistributions(u_ds);
	return r;
}

static bool_t foundinorigins(struct update_origin *origins,size_t nameoffset,const char *name) {
	struct update_origin *o;

	for( o = origins; o != NULL ; o = o->next ) {
		if( o->releasefile == NULL )
			continue;
		assert(strlen(o->releasefile) > nameoffset);
		if( strcmp(name,o->releasefile+nameoffset) == 0 )
			return TRUE;
		if( o->releasegpgfile == NULL )
			continue;
		assert(strlen(o->releasegpgfile) > nameoffset);
		if( strcmp(name,o->releasegpgfile+nameoffset) == 0 )
			return TRUE;
	}
	return FALSE;
}

static bool_t foundinindices(struct update_target *targets,size_t nameoffset,const char *name) {
	struct update_target *t;
	struct update_index *i;

	for( t = targets; t != NULL ; t = t->next ) {
		for( i = t->indices ; i != NULL ; i=i->next ) {
			size_t l;

			if( i->filename == NULL )
				continue;
			l = strlen(i->filename);
			assert(l > nameoffset);
			if( strncmp(name,i->filename+nameoffset,l-nameoffset) == 0 &&
				(name[l-nameoffset] == '\0' ||
				 strcmp(name+(l-nameoffset), ".done") == 0) )
				return TRUE;
		}
	}
	return FALSE;
}

static retvalue listclean_distribution(const char *listdir,DIR *dir, const char *pattern,
		struct update_origin *origins,
		struct update_target *targets) {
	struct dirent *r;
	size_t patternlen = strlen(pattern);
	size_t nameoffset = strlen(listdir)+1;

	while( TRUE ) {
		size_t namelen;
		char *fullfilename;
		int e;

		r = readdir(dir);
		if( r == NULL ) {
			e = errno;
			if( e == 0 )
				return RET_OK;
			/* this should not happen... */
			e = errno;
			fprintf(stderr,"Error reading dir '%s': %d=%m!\n",listdir,e);
			return RET_ERRNO(e);
		}
		namelen = _D_EXACT_NAMLEN(r);
		if( namelen < patternlen || strncmp(pattern,r->d_name,patternlen) != 0)
			continue;
		if( foundinorigins(origins,nameoffset,r->d_name) )
			continue;
		if( foundinindices(targets,nameoffset,r->d_name) )
			continue;
		fullfilename = calc_dirconcat(listdir,r->d_name);
		if( fullfilename == NULL )
			return RET_ERROR_OOM;
		if( verbose >= 0 ) {
			size_t l = strlen(r->d_name);
			if( l < 9 || strcmp(r->d_name+(l-8),"_changed") != 0 )
				fprintf(stderr,
"Removing apparently leftover file '%s'.\n"
"(Use --keepunneededlists to avoid this in the future.)\n",fullfilename);
		}
		e = unlink(fullfilename);
		if( e != 0 ) {
			e = errno;
			fprintf(stderr,"Error unlinking '%s': %d=%m.\n",fullfilename,e);
			free(fullfilename);
			return RET_ERRNO(e);
		}
		free(fullfilename);
	}
	return RET_OK;
}

retvalue updates_clearlists(const char *listdir,struct update_distribution *distributions) {
	struct update_distribution *d;

	for( d = distributions ; d != NULL ; d = d->next ) {
		char *pattern;
		retvalue r;
		DIR *dir;

		pattern = calc_downloadedlistpattern(d->distribution->codename);
		if( pattern == NULL )
			return RET_ERROR_OOM;
		// TODO: check if it is always created before...
		dir = opendir(listdir);
		if( dir == NULL ) {
			int e = errno;
			fprintf(stderr,"Error opening directory '%s' (error %d=%m)!",listdir,e);
			free(pattern);
			return RET_ERRNO(e);
		}
		r = listclean_distribution(listdir,dir,pattern,
				d->origins,
				d->targets);
		free(pattern);
		closedir(dir);
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
	}
	return RET_OK;
}

/************************* Preparations *********************************/
static inline retvalue startuporigin(struct aptmethodrun *run,struct update_origin *origin) {
	retvalue r;
	struct aptmethod *method;

	assert( origin != NULL && origin->pattern != NULL );
	r = aptmethod_newmethod(run,origin->pattern->method,
			origin->pattern->fallback,
			origin->pattern->config,&method);
	if( RET_WAS_ERROR(r) ) {
		origin->download = NULL;
		origin->failed = TRUE;
		return r;
	}
	origin->download = method;
	return RET_OK;
}

static retvalue updates_startup(struct aptmethodrun *run,struct update_distribution *distributions, int force) {
	retvalue result,r;
	struct update_origin *origin;
	struct update_distribution *d;

	result = RET_NOTHING;
	for( d=distributions ; d != NULL ; d=d->next) {
		if( d->distribution->deb_override != NULL || 
		    d->distribution->dsc_override != NULL ||
		    d->distribution->udeb_override != NULL ) {
			if( verbose >= 0 )
				fprintf(stderr,"Warning: Override-Files of '%s' ignored as not yet supported while updating!\n",d->distribution->codename);
		}
		if( d->distribution->tracking != dt_NONE ) {
			fprintf(stderr,"WARNING: Updating does not update trackingdata. Trackingdata of %s will be outdated!\n",d->distribution->codename);
		}
		for( origin=d->origins; origin != NULL ; origin=origin->next ) {
			if( origin->pattern == NULL)
				continue;
			r = startuporigin(run,origin);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				return r;
		}
	}
	return result;
}
/******************* Fetch all Lists for an update **********************/
static inline retvalue queuemetalists(struct update_origin *origin) {
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

/* returns RET_NOTHING when nothing new will be there */
static inline retvalue queueindex(struct update_index *index,int force) {
	const struct update_origin *origin = index->origin;
	int i;
	size_t l;

	assert( index != NULL && index->origin != NULL );
	index->checksum_ofs = -1;
	if( origin->download == NULL || origin->failed ) {
		return RET_NOTHING;
	}
	if( origin->releasefile == NULL ) {
		retvalue r;
		/* TODO: save the content to make sure it is the old instead */
		donefile_delete(index->filename);
		r = aptmethod_queueindexfile(origin->download,
				index->upstream,index->filename);
		assert( r != RET_NOTHING );
		return r;
	}

	//TODO: this is a very crude hack, think of something better...
	l = 7 + strlen(origin->suite_from); // == strlen("dists/%s/")

	for( i = 0 ; i+1 < origin->checksums.count ; i+=2 ) {

		assert( strlen(index->upstream) > l );
		if( strcmp(index->upstream+l,origin->checksums.values[i]) == 0 ){
			retvalue r;
			const char *md5sum = origin->checksums.values[i+1];

			index->checksum_ofs = i+1;

			r = md5sum_ensure(index->filename,md5sum,FALSE);
			if( r == RET_NOTHING ) {
				donefile_delete(index->filename);
				r = aptmethod_queuefile(origin->download,
					index->upstream,index->filename,
					md5sum,NULL,NULL);
			} else if( RET_IS_OK(r) ) {
				/* file is already there, but it might still need
				 * processing as last time it was not due to some
				 * error of some other file */
				r = donefile_isold(index->filename, md5sum);
			}
			return r;
		}
		
	}
	fprintf(stderr,"Could not find '%s' within the Releasefile of '%s':\n'%s'\n",index->upstream,origin->pattern->name,origin->releasefile);
	if( force > 2 ) {
		donefile_delete(index->filename);
		aptmethod_queueindexfile(origin->download,
				index->upstream,index->filename);
	}
	return RET_ERROR_WRONG_MD5;
}



static retvalue updates_queuemetalists(struct update_distribution *distributions, int force) {
	retvalue result,r;
	struct update_origin *origin;
	struct update_distribution *d;

	result = RET_NOTHING;
	for( d=distributions ; d != NULL ; d=d->next) {
		for( origin=d->origins; origin != NULL ; origin=origin->next ) {
			if( origin->pattern == NULL)
				continue;
			if( origin->pattern->ignorerelease )
				continue;
			r = queuemetalists(origin);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				return r;
		}

	}

	return result;
}

static retvalue updates_queuelists(struct update_distribution *distributions,bool_t skipold,bool_t *anythingtodo,int force) {
	retvalue result,r;
	struct update_origin *origin;
	struct update_target *target;
	struct update_index *index;
	struct update_distribution *d;

	result = RET_NOTHING;
	for( d=distributions ; d != NULL ; d=d->next) {

		for( origin=d->origins; origin != NULL ; origin=origin->next ) {
			if( origin->pattern == NULL)
				continue;
			r = readchecksums(origin);
			RET_UPDATE(result,r);
			//TODO: more force needed?
			if( RET_WAS_ERROR(r) && force <= 1 ) {
				if( force == 1 ) {
					fprintf(stderr,"To ignore errors checking Release files specify --force two times.\n");
				}
				return r;
			}
		}
		for( target=d->targets; target!=NULL ; target=target->next ) {
			target->nothingnew = skipold;
			target->incomplete = FALSE;
			for( index=target->indices ; index!=NULL ; index=index->next ) {
				if( index->origin == NULL )
					continue;
				r = queueindex(index,force);
				if( RET_IS_OK(r) ) {
					target->nothingnew = FALSE;
					*anythingtodo = TRUE;
				}
				if( RET_WAS_ERROR(r) )
					index->failed = TRUE;
				RET_UPDATE(result,r);
				if( RET_WAS_ERROR(r) && force <= 0 )
					return r;
			}
		}
	}
	return result;
}

static retvalue calllisthook(const char *listhook,struct update_index *index) {
	char *newfilename;
	pid_t f,c;
	int status; 

	newfilename = mprintf("%s_changed",index->filename);
	if( newfilename == NULL )
		return RET_ERROR_OOM;
	f = fork();
	if( f < 0 ) {
		int err = errno;
		free(newfilename);
		fprintf(stderr,"Error while forking for listhook: %d=%m\n",err);
		return RET_ERRNO(err);
	}
	if( f == 0 ) {
		long maxopen;
		/* Try to close all open fd but 0,1,2 */
		maxopen = sysconf(_SC_OPEN_MAX);
		if( maxopen > 0 ) {
			int fd;
			for( fd = 3 ; fd < maxopen ; fd++ )
				close(fd);
		} // otherweise we have to hope...
		execl(listhook,listhook,index->filename,newfilename,NULL);
		fprintf(stderr,"Error while executing '%s': %d=%m\n",listhook,errno);
		exit(255);
	}
	if( verbose > 5 )
		fprintf(stderr,"Called %s '%s# '%s'\n",listhook,index->filename,newfilename);
	assert(index->original_filename == NULL);
	index->original_filename = index->filename;
	index->filename=newfilename;
	do {
		c = waitpid(f,&status,WUNTRACED);
		if( c < 0 ) {
			int err = errno;
			fprintf(stderr,"Error while waiting for hook '%s' to finish: %d=%m\n",listhook,err);
			return RET_ERRNO(err);
		}
	} while( c != f );
	if( WIFEXITED(status) ) {
		if( WEXITSTATUS(status) == 0 ) {
			if( verbose > 5 )
				fprintf(stderr,"Listhook successfully returned!\n");
			return RET_OK;
		} else {
			fprintf(stderr,"Listhook failed with exitcode %d!\n",(int)WEXITSTATUS(status));
			return RET_ERROR;
		}
	} else {
		fprintf(stderr,"Listhook terminated abnormaly. (status is %x)!\n",status);
		return RET_ERROR;
	}
}

static retvalue updates_calllisthooks(struct update_distribution *distributions,int force) {
	retvalue result,r;
	struct update_target *target;
	struct update_index *index;
	struct update_distribution *d;

	result = RET_NOTHING;
	for( d=distributions ; d != NULL ; d=d->next) {

		for( target=d->targets; target!=NULL ; target=target->next ) {
			if( target->nothingnew )
				continue;
			for( index=target->indices ; index != NULL ; 
						     index=index->next ) {
				if( index->origin == NULL )
					continue;
				if( index->origin->pattern->listhook == NULL )
					continue;
				if( index->failed || index->origin->failed ) {
					continue;
				}
				r = calllisthook(index->origin->pattern->listhook,index);
				if( RET_WAS_ERROR(r) ) {
					index->failed = TRUE;
					if( force <= 0 )
						return r;
				}
				RET_UPDATE(result,r);
			}
		}
	}
	return result;
}

static upgrade_decision ud_decide_by_pattern(void *privdata, const char *package,UNUSED(const char *old_version),UNUSED(const char *new_version),const char *newcontrolchunk) {
	struct update_pattern *pattern = privdata;
	retvalue r;

	switch( filterlist_find(package,&pattern->filterlist) ) {
		case flt_deinstall:
		case flt_purge:
			return UD_NO;
		case flt_hold:
			return UD_HOLD;
		case flt_error:
			/* cannot yet be handled! */
			fprintf(stderr,"Packagename marked to be unexpected('error'): '%s'!\n",package);
			return UD_ERROR;
		case flt_install:
			break;
	}

	if( pattern->includecondition != NULL ) {
		r = term_decidechunk(pattern->includecondition,newcontrolchunk);
		if( RET_WAS_ERROR(r) )
			return UD_ERROR;
		if( r == RET_NOTHING ) {
			return UD_NO;
		}
	}

	return UD_UPGRADE;
}

static inline retvalue searchformissing(const char *dbdir,struct update_target *u,int force) {
	struct update_index *index;
	retvalue result,r;

	if( u->nothingnew ) {
		if( verbose >= 0 ) {
		fprintf(stderr,"  nothing new for '%s' (use --noskipold to process anyway)\n",u->target->identifier);
		}
		return RET_NOTHING;
	}
	if( verbose > 2 )
		fprintf(stderr,"  processing updates for '%s'\n",u->target->identifier);
	r = upgradelist_initialize(&u->upgradelist,u->target,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;

	for( index=u->indices ; index != NULL ; index=index->next ) {

		if( index->origin == NULL ) {
			if( verbose > 4 )
				fprintf(stderr,"  marking everything to be deleted\n");
			r = upgradelist_deleteall(u->upgradelist);
			if( RET_WAS_ERROR(r) )
				u->incomplete = TRUE;
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				return result;
			u->ignoredelete = FALSE;
			continue;
		}

		if( index->failed || index->origin->failed ) {
			if( verbose >= 1 )
				fprintf(stderr,"  missing '%s'\n",index->filename);
			u->incomplete = TRUE;
			u->ignoredelete = TRUE;
			continue;
		}

		if( verbose > 4 )
			fprintf(stderr,"  reading '%s'\n",index->filename);
		assert(index->origin->download!= NULL);
		r = upgradelist_update(u->upgradelist,
				index->origin->download,index->filename,
				ud_decide_by_pattern,
				(void*)index->origin->pattern,
				force);
		if( RET_WAS_ERROR(r) ) {
			u->incomplete = TRUE;
			u->ignoredelete = TRUE;
		}
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			return result;
	}
	
	return result;
}

static retvalue updates_readindices(const char *dbdir,struct update_distribution *d,int force) {
	retvalue result,r;
	struct update_target *u;

	result = RET_NOTHING;
	for( u=d->targets ; u != NULL ; u=u->next ) {
		r = searchformissing(dbdir,u,force);
		if( RET_WAS_ERROR(r) )
			u->incomplete = TRUE;
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
	}
	return result;
}

static retvalue updates_enqueue(struct downloadcache *cache,filesdb filesdb,struct update_distribution *distribution,int force) {
	retvalue result,r;
	struct update_target *u;

	result = RET_NOTHING;
	for( u=distribution->targets ; u != NULL ; u=u->next ) {
		if( u->nothingnew )
			continue;
		r = upgradelist_enqueue(u->upgradelist,cache,filesdb,force);
		if( RET_WAS_ERROR(r) )
			u->incomplete = TRUE;
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
	}
	return result;
}


static retvalue updates_install(const char *dbdir,filesdb filesdb,references refs,struct update_distribution *distribution,int force,struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	struct update_target *u;

	result = RET_NOTHING;
	for( u=distribution->targets ; u != NULL ; u=u->next ) {
		if( u->nothingnew )
			continue;
		r = upgradelist_install(u->upgradelist,dbdir,filesdb,refs,force,u->ignoredelete,dereferencedfilekeys);
		if( RET_WAS_ERROR(r) )
			u->incomplete = TRUE;
		RET_UPDATE(result,r);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
	}
	return result;
}

static void markdone(struct update_target *target) {
	struct update_index *index;

	if( target->incomplete )
		return;

	for( index=target->indices ; index != NULL ; index=index->next ) {
		const struct update_origin *origin = index->origin;
		const char *md5sum;

		if( origin == NULL )
			/* No need to mark a delete rule as done */
			continue;
		if( origin->releasefile == NULL )
			/* TODO: once they can be detected as
			 * old, also generate donefile here */
			continue;
		if( index->checksum_ofs < 0 )
			continue;
		assert( index->checksum_ofs < origin->checksums.count );
		md5sum = origin->checksums.values[index->checksum_ofs];
		if( index->original_filename == NULL )
			donefile_create(index->filename, md5sum);
		else
			donefile_create(index->original_filename, md5sum);
	}
}

static void updates_dump(struct update_distribution *distribution) {
	struct update_target *u;

	for( u=distribution->targets ; u != NULL ; u=u->next ) {
		printf("Updates needed for '%s':\n",u->target->identifier);
		upgradelist_dump(u->upgradelist);
	}
}

static retvalue updates_downloadlists(const char *methoddir,struct aptmethodrun *run,struct update_distribution *distributions, bool_t skipold, bool_t *anythingtodo, int force) {
	retvalue r,result;

	/* first get all "Release" and "Release.gpg" files */
	result = updates_queuemetalists(distributions,force);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		return result;
	}

	r = aptmethod_download(run,methoddir,NULL);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(r) && force <= 0 ) {
		return result;
	}

	/* Then get all index files (with perhaps md5sums from the above) */
	r = updates_queuelists(distributions,skipold,anythingtodo,force);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		return result;
	}

	r = aptmethod_download(run,methoddir,NULL);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(r) && force <= 0 ) {
		return result;
	}

	return result;
}

retvalue updates_update(const char *dbdir,const char *methoddir,filesdb filesdb,references refs,struct update_distribution *distributions,int force,bool_t nolistsdownload,bool_t skipold,struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	struct update_distribution *d;
	struct aptmethodrun *run;
	struct downloadcache *cache;

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) )
		return r;

	if( nolistsdownload ) {
		if( skipold && verbose >= 0 ) {
			fprintf(stderr,"Ignoring --skipold because of --nolistsdownload\n");
		}
		skipold = FALSE;
	}

	/* preperations */
	result = updates_startup(run,distributions,force);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		aptmethod_shutdown(run);
		return result;
	}
	if( nolistsdownload ) {
		if( verbose >= 0 )
			fprintf(stderr,"Warning: As --nolistsdownload is given, index files are NOT checked.\n");
	} else {
		bool_t anythingtodo = !skipold;

		r = updates_downloadlists(methoddir,run,distributions,skipold,&anythingtodo,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(result) && force <= 0 ) {
			aptmethod_shutdown(run);
			return result;
		}
		/* TODO: 
		 * add a check if some of the upstreams without Release files
		 * are unchanged and if this changes anything? */
		if( !anythingtodo ) {
			fprintf(stderr,"Nothing to do found. (Use --noskipold to force processing)\n");
			aptmethod_shutdown(run);
			if( RET_IS_OK(result) )
				return RET_NOTHING;
			else
				return result;
		}
	}
	/* Call ListHooks (if given) on the downloaded index files.
	 * (This is done even when nolistsdownload is given, as otherwise
	 *  the filename to look in is not changed) */
	r = updates_calllisthooks(distributions,force);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
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
	
	for( d=distributions ; d != NULL ; d=d->next) {
		r = updates_readindices(dbdir,d,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
		r = updates_enqueue(cache,filesdb,d,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
	}
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		aptmethod_shutdown(run);
		return result;
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

	if( RET_WAS_ERROR(result) && force <= 0 ) {
		return result;
	}
	if( verbose >= 0 )
		fprintf(stderr,"Installing (and possibly deleting) packages...\n");

	for( d=distributions ; d != NULL ; d=d->next) {
		r = updates_install(dbdir,filesdb,refs,d,force,dereferencedfilekeys);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
	}

	for( d=distributions ; d != NULL ; d=d->next) {
		struct update_target *u;

		for( u=d->targets ; u != NULL ; u=u->next ) {
			markdone(u);
		}
	}

	return result;
}

retvalue updates_checkupdate(const char *dbdir,const char *methoddir,struct update_distribution *distributions,int force,bool_t nolistsdownload,bool_t skipold) {
	struct update_distribution *d;
	retvalue result,r;
	struct aptmethodrun *run;

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) )
		return r;

	if( nolistsdownload ) {
		if( skipold && verbose >= 0 ) {
			fprintf(stderr,"Ignoring --skipold because of --nolistsdownload\n");
		}
		skipold = FALSE;
	}

	result = updates_startup(run,distributions,force);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		aptmethod_shutdown(run);
		return result;
	}
	if( nolistsdownload ) {
		if( verbose >= 0 )
			fprintf(stderr,"Warning: As --nolistsdownload is given, index files are NOT checked.\n");
	} else {
		bool_t anythingtodo = !skipold;
		r = updates_downloadlists(methoddir,run,distributions,skipold,&anythingtodo,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(result) && force <= 0 ) {
			aptmethod_shutdown(run);
			return result;
		}
		if( !anythingtodo ) {
			fprintf(stderr,"Nothing to do found. (Use --noskipold to force processing)\n");
			return result;
		}
	}
	/* Call ListHooks (if given) on the downloaded index files.
	 * (This is done even when nolistsdownload is given, as otherwise
	 *  the filename to look in is not changed) */
	r = updates_calllisthooks(distributions,force);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		return result;
	}
	if( verbose > 0 )
		fprintf(stderr,"Shutting down aptmethods...\n");
	r = aptmethod_shutdown(run);
	RET_UPDATE(result,r);
	if( RET_WAS_ERROR(result) && force <= 0 ) {
		return result;
	}

	/* Then look what packages to get */
	if( verbose >= 0 )
		fprintf(stderr,"Calculating packages to get...\n");
	
	for( d=distributions ; d != NULL ; d=d->next) {
		r = updates_readindices(dbdir,d,force);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
		updates_dump(d);
	}

	return result;
}

static retvalue singledistributionupdate(const char *dbdir,const char *methoddir,filesdb filesdb,references refs,struct update_distribution *d,int force,bool_t nolistsdownload,bool_t skipold, struct strlist *dereferencedfilekeys) {
	struct aptmethodrun *run;
	struct downloadcache *cache;
	struct update_origin *origin;
	struct update_target *target;
	retvalue result,r;

	result = RET_OK;

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) )
		return r;

	/* preperations */
	for( origin=d->origins; origin != NULL ; origin=origin->next ) {
		if( origin->pattern == NULL)
			continue;
		r = startuporigin(run,origin);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 ) {
			aptmethod_shutdown(run);
			return r;
		}
	}
	if( !nolistsdownload ) {
		for( origin=d->origins; origin != NULL ; origin=origin->next ) {
			if( origin->pattern == NULL)
				continue;
			if( origin->pattern->ignorerelease )
				continue;
			r = queuemetalists(origin);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) ) {
				if( force <= 0 ) {
					aptmethod_shutdown(run);
					return r;
				}
				origin->failed = TRUE;
			}
		}
		r = aptmethod_download(run,methoddir,NULL);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 ) {
			aptmethod_shutdown(run);
			return r;
		}
		for( origin=d->origins; origin != NULL ; origin=origin->next ) {
			if( origin->pattern == NULL)
				continue;
			r = readchecksums(origin);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 1 ) {
				if( force == 1 ) {
					fprintf(stderr,"To ignore errors checking Release files specify --force two times.\n");
				}
				aptmethod_shutdown(run);
				return r;
			}
		}
	}

	for( target=d->targets; target!=NULL ; target=target->next ) {
		struct update_index *index;
		target->nothingnew = skipold;
		target->incomplete = FALSE;
		if( !nolistsdownload ) {
			for( index=target->indices ; index!=NULL ; index=index->next ) {
				if( index->origin == NULL || index->origin->failed )
					continue;
				r = queueindex(index,force);
				if( RET_WAS_ERROR(r) ) {
					RET_UPDATE(result,r);
					if( force <= 0 ) {
						aptmethod_shutdown(run);
						return r;
					}
					index->failed = TRUE;
				} else if( RET_IS_OK(r) ) {
					target->nothingnew = FALSE;
				}
			}
			if( target->nothingnew )
				continue;
			r = aptmethod_download(run,methoddir,NULL);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 ) {
				aptmethod_shutdown(run);
				return r;
			}
		}

		for( index=target->indices ; index != NULL ; index=index->next ) {
			if( index->origin == NULL )
				continue;
			if( index->origin->pattern->listhook == NULL )
				continue;
			if( index->failed || index->origin->failed ) {
				continue;
			}
			/* Call ListHooks (if given) on the downloaded index files. */
			r = calllisthook(index->origin->pattern->listhook,index);
			if( RET_WAS_ERROR(r) ) {
				index->failed = TRUE;
				RET_UPDATE(result,r);
				if( force <= 0) {
					aptmethod_shutdown(run);
					return r;
				}
			}

		}
		/* Then get all packages */
		if( verbose >= 0 )
			fprintf(stderr,"Calculating packages to get for %s's %s...\n",d->distribution->codename,target->target->identifier);
		r = downloadcache_initialize(&cache);
		RET_UPDATE(result,r);	
		if( !RET_IS_OK(r) ) {
			(void)downloadcache_free(cache);
			if( RET_WAS_ERROR(r) && force <= 0 ) {
				aptmethod_shutdown(run);
				return result;
			}
			continue;
		}
		r = searchformissing(dbdir,target,force);
		if( RET_WAS_ERROR(r) )
			target->incomplete = TRUE;
		else if( r == RET_NOTHING ) {
			(void)downloadcache_free(cache);
			continue;
		}
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 ) {
			(void)downloadcache_free(cache);
			aptmethod_shutdown(run);
			return result;
		}
		r = upgradelist_enqueue(target->upgradelist,cache,filesdb,force);
		if( RET_WAS_ERROR(r) )
			target->incomplete = TRUE;
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 ) {
			(void)downloadcache_free(cache);
			aptmethod_shutdown(run);
			return result;
		}
		if( verbose >= 0 )
			fprintf(stderr,"Getting packages for %s's %s...\n",d->distribution->codename,target->target->identifier);
		r = aptmethod_download(run,methoddir,filesdb);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 ) {
			(void)downloadcache_free(cache);
			aptmethod_shutdown(run);
			return result;
		}
		r = downloadcache_free(cache);
		RET_UPDATE(result,r);

		if( verbose >= 0 )
			fprintf(stderr,"Installing/removing packages for %s's %s...\n",d->distribution->codename,target->target->identifier);
		r = upgradelist_install(target->upgradelist,dbdir,filesdb,refs,force,target->ignoredelete,dereferencedfilekeys);
		if( RET_WAS_ERROR(r) )
			target->incomplete = TRUE;
		RET_UPDATE(result,r);
		upgradelist_free(target->upgradelist);
		target->upgradelist = NULL;
		if( RET_WAS_ERROR(r) && force <= 0 ) {
			aptmethod_shutdown(run);
			return result;
		}
		markdone(target);
	}
	if( verbose > 0 )
		fprintf(stderr,"Shutting down aptmethods...\n");
	r = aptmethod_shutdown(run);
	RET_UPDATE(result,r);

	return result;
}

retvalue updates_iteratedupdate(const char *confdir,const char *dbdir,const char *distdir,const char *methoddir,filesdb filesdb,references refs,struct update_distribution *distributions,int force,bool_t nolistsdownload,bool_t skipold,struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	struct update_distribution *d;

	if( nolistsdownload ) {
		if( verbose >= 0 )
			fprintf(stderr,"Warning: As --nolistsdownload is given, index files are NOT checked.\n");
		if( skipold && verbose >= 0 ) {
			fprintf(stderr,"Ignoring --skipold because of --nolistsdownload\n");
		}
		skipold = FALSE;
	}

	result = RET_NOTHING;
	for( d=distributions ; d != NULL ; d=d->next) {
		if( d->distribution->deb_override != NULL || 
		    d->distribution->dsc_override != NULL ||
		    d->distribution->udeb_override!= NULL ) {
			if( verbose >= 0 )
				fprintf(stderr,"Warning: Override-Files of '%s' ignored as not yet supported while updating!\n",d->distribution->codename);
		}
		if( d->distribution->tracking != dt_NONE ) {
			fprintf(stderr,"WARNING: Updating does not update trackingdata. Trackingdata of %s will be outdated!\n",d->distribution->codename);
		}
		r = singledistributionupdate(dbdir,methoddir,filesdb,refs,d,force,nolistsdownload,skipold,dereferencedfilekeys);
		RET_UPDATE(result,r);
		if( RET_IS_OK(r) ) {
			r = distribution_export(d->distribution,confdir,dbdir,distdir,TRUE);
			RET_UPDATE(result,r);
		}
	}
	return result;
}
