/*  This file is part of "reprepro"
 *  Copyright (C) 2006 Bernhard R. Link
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "md5sum.h"
#include "pull.h"
#include "upgradelist.h"
#include "distribution.h"
#include "terms.h"
#include "filterlist.h"

extern int verbose;

/***************************************************************************
 * step one:                                                               *
 * parse CONFDIR/pull to get pull information saved in                     *
 * pull_rule structs                                                    *
 **************************************************************************/

/* the data for some upstream part to get pull from, some
 * some fields can be NULL or empty */
struct pull_rule {
	struct pull_rule *next;
	//e.g. "Name: woody"
	char *name;
	//e.g. "From: woody"
	char *from;
	//e.g. "Architectures: i386 sparc mips" (empty means all)
	struct strlist architectures_from;
	struct strlist architectures_into;
	//e.g. "Components: main contrib" (empty means all)
	struct strlist components;
	//e.g. "UDebComponents: main" // (empty means all)
	struct strlist udebcomponents;
	// NULL means no condition
	/*@null@*/term *includecondition;
	struct filterlist filterlist;
	/*----only set after _addsourcedistribution----*/
	/*@NULL@*/ struct distribution *distribution;
	bool_t used;
};

static void pull_rule_free(/*@only@*/struct pull_rule *pull) {
	if( pull == NULL )
		return;
	free(pull->name);
	free(pull->from);
	strlist_done(&pull->architectures_from);
	strlist_done(&pull->architectures_into);
	strlist_done(&pull->components);
	strlist_done(&pull->udebcomponents);
	term_free(pull->includecondition);
	filterlist_release(&pull->filterlist);
	free(pull);
}

void pull_freerules(struct pull_rule *p) {
	while( p != NULL ) {
		struct pull_rule *rule;

		rule = p;
		p = rule->next;
		pull_rule_free(rule);
	}
}

inline static retvalue parse_rule(const char *confdir,const char *chunk, struct pull_rule **rule) {
	struct pull_rule *pull;
	struct strlist architectureslist;
	char *formula,*filename;
	retvalue r;
	static const char * const allowedfields[] = {"Name", "From",
"Architectures", "Components", "UDebComponents",
"FilterFormula", "FilterList", NULL};

	pull = calloc(1,sizeof(struct pull_rule));
	if( pull == NULL )
		return RET_ERROR_OOM;
	r = chunk_getvalue(chunk,"Name",&pull->name);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Unexpected chunk in pulls-file: '%s'.\n",chunk);
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(pull);
		return r;
	}
	if( verbose > 30 ) {
		fprintf(stderr,"parsing pull-chunk '%s'\n",pull->name);
	}

	/* * Look where we are getting it from: * */

	r = chunk_getvalue(chunk,"From",&pull->from);
	if( !RET_IS_OK(r) ) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"No From found in pull-block %s!\n",
					pull->name);
			r = RET_ERROR_MISSING;
		}
		pull_rule_free(pull);
		return r;
	}

	r = chunk_checkfields(chunk,allowedfields,TRUE);
	if( RET_WAS_ERROR(r) ) {
		pull_rule_free(pull);
		return r;
	}

	/* * Check which architectures to pull from * */
	r = chunk_getuniqwordlist(chunk,"Architectures",&architectureslist);
	// TODO: is this save if uniqwordlist could become sorted?
	if( RET_WAS_ERROR(r) ) {
		pull_rule_free(pull);
		return r;
	}
	if( r == RET_NOTHING ) {
		pull->architectures_from.count = 0;
		pull->architectures_into.count = 0;
	} else {
		r = splitlist(&pull->architectures_from,
				&pull->architectures_into,&architectureslist);
		strlist_done(&architectureslist);
		if( RET_WAS_ERROR(r) ) {
			pull_rule_free(pull);
			return r;
		}
	}

	/* * Check which components to pull from * */
	r = chunk_getuniqwordlist(chunk,"Components",&pull->components);
	if( RET_WAS_ERROR(r) ) {
		pull_rule_free(pull);
		return r;
	}
	if( r == RET_NOTHING ) {
		pull->components.count = 0;
	}

	/* * Check which components to pull udebs from * */
	r = chunk_getuniqwordlist(chunk,"UDebComponents",&pull->udebcomponents);
	if( RET_WAS_ERROR(r) ) {
		pull_rule_free(pull);
		return r;
	}
	if( r == RET_NOTHING ) {
		pull->udebcomponents.count = 0;
	}

	/* * Check if there is a Include condition * */
	r = chunk_getvalue(chunk,"FilterFormula",&formula);
	if( RET_WAS_ERROR(r) ) {
		pull_rule_free(pull);
		return r;
	}
	if( r != RET_NOTHING ) {
		r = term_compile(&pull->includecondition,formula,
			T_OR|T_BRACKETS|T_NEGATION|T_VERSION|T_NOTEQUAL);
		free(formula);
		if( RET_WAS_ERROR(r) ) {
			pull->includecondition = NULL;
			pull_rule_free(pull);
			return r;
		}
		assert( r != RET_NOTHING );
	}
	/* * Check if there is a list to say what can be included by pull * */
	r = chunk_getvalue(chunk,"FilterList",&filename);
	if( RET_WAS_ERROR(r) ) {
		pull_rule_free(pull);
		return r;
	}
	if( r != RET_NOTHING ) {
		r = filterlist_load(&pull->filterlist,confdir,filename);
		free(filename);
		if( RET_WAS_ERROR(r) ) {
			pull_rule_free(pull);
			return r;
		}
		assert( r != RET_NOTHING );
	} else {
		filterlist_empty(&pull->filterlist,flt_install);
	}
	pull->distribution = NULL;
	pull->used = FALSE;

	*rule = pull;
	return RET_OK;
}

struct getrules_data {
	struct pull_rule **rules;
	const char *confdir;
};

static retvalue pull_parsechunk(void *data,const char *chunk) {
	struct pull_rule *pull;
	struct getrules_data *d = data;
	retvalue r;

	r = parse_rule(d->confdir,chunk,&pull);
	if( RET_IS_OK(r) ) {
		pull->next = *d->rules;
		*d->rules = pull;
	}
	return r;
}

retvalue pull_getrules(const char *confdir,struct pull_rule **rules) {
	char *pullfile;
	struct pull_rule *pull = NULL;
	struct getrules_data data;
	retvalue r;

	pullfile = calc_dirconcat(confdir,"pulls");
	if( pullfile == NULL )
		return RET_ERROR_OOM;
	data.rules = &pull;
	data.confdir = confdir;
	r = chunk_foreach(pullfile,pull_parsechunk,&data,FALSE);
	free(pullfile);
	if( RET_IS_OK(r) )
		*rules = pull;
	else if( r == RET_NOTHING ) {
		*rules = NULL;
		r = RET_OK;
	}
	return r;
}

/***************************************************************************
 * step two:                                                               *
 * create pull_distribution structs to hold all additional information for *
 * a distribution                                                          *
 **************************************************************************/

struct pull_target;
static void pull_freetargets(struct pull_target *targets);

struct pull_distribution {
	struct pull_distribution *next;
	/*@dependant@*/struct distribution *distribution;
	struct pull_target *targets;
	/*@dependant@*/struct pull_rule *rules[];
};

void pull_freedistributions(struct pull_distribution *d) {
	while( d != NULL ) {
		struct pull_distribution *next;

		next = d->next;
		pull_freetargets(d->targets);
		free(d);
		d = next;
	}
}

static retvalue pull_initdistribution(struct pull_distribution **pp,
		struct distribution *distribution,
		struct pull_rule *rules) {
	struct pull_distribution *p;
	int i;

	assert(distribution != NULL);
	if( distribution->pulls.count == 0 )
		return RET_NOTHING;

	p = malloc(sizeof(struct pull_distribution)+
		sizeof(struct pull_rules *)*distribution->pulls.count);
	if( p == NULL )
		return RET_ERROR_OOM;
	p->next = NULL;
	p->distribution = distribution;
	p->targets = NULL;
	for( i = 0 ; i < distribution->pulls.count ; i++ ) {
		const char *name = distribution->pulls.values[i];
		if( strcmp(name,"-") == 0 ) {
			p->rules[i] = NULL;
		} else {
			struct pull_rule *rule = rules;
			while( rule && strcmp(rule->name,name) != 0 )
				rule = rule->next;
			if( rule == NULL ) {
				fprintf(stderr,
"Error: Unknown pull-rule '%s' in distribution '%s'!\n",
						name, distribution->codename);
				return RET_ERROR_MISSING;
			}
			p->rules[i] = rule;
			rule->used = TRUE;
		}
	}
	*pp = p;
	return RET_OK;
}

static retvalue pull_init(struct pull_distribution **pulls,
		struct pull_rule *rules,
		struct distribution *distributions) {
	struct pull_distribution *p = NULL, **pp = &p;
	struct distribution *d;
	retvalue r;

	for( d = distributions ; d != NULL ; d = d->next ) {
		r = pull_initdistribution(pp, d, rules);
		if( RET_WAS_ERROR(r) ) {
			pull_freedistributions(p);
			return r;
		}
		if( RET_IS_OK(r) ) {
			assert( *pp != NULL );
			pp = &(*pp)->next;
		}
	}
	*pulls = p;
	return RET_OK;
}

/***************************************************************************
 * step three:                                                             *
 * load the config of the distributions mentioned in the rules             *
 **************************************************************************/

static inline void pull_addsourcedistribution(struct pull_rule *rules,
		struct distribution *distribution) {
	struct pull_rule *rule;

	for( rule = rules ; rule != NULL ; rule = rule->next ) {
		if( strcmp(rule->from, distribution->codename) == 0 )
			rule->distribution = distribution;
	}
}

static inline void pull_addsourcedistributions(struct pull_rule *rules,
		struct distribution *distributions) {
	struct distribution *d;

	for( d = distributions ; d != NULL ; d = d->next ) {
		pull_addsourcedistribution(rules, d);
	}
}

static retvalue pull_loadmissingsourcedistributions(const char *confdir,
		struct pull_rule *rules,
		struct distribution **extradistributions) {
	const char **names = NULL;
	struct pull_rule *rule;
	size_t count = 0;
	retvalue r;

	for( rule = rules ; rule != NULL ; rule = rule->next ) {
		if( rule->used && rule->distribution == NULL ) {
			unsigned int i;

			for( i = 0 ; i < count ; i++ ) {
				if( strcmp(names[i],rule->from) == 0 )
					break;
			}
			if( i != count )
				break;

			if( (count & 7) == 0 ) {
				const char **n = realloc(names,
					(count+8)* sizeof(const char*));
				if( n == NULL ) {
					free(names);
					return RET_ERROR_OOM;
				}
				names = n;
			}
			names[count++] = rule->from;
		}
	}
	if( count == 0 ) {
		*extradistributions = NULL;
		return RET_OK;
	}
	r = distribution_getmatched(confdir, count, names, extradistributions, FALSE);
	free(names);
	assert( r != RET_NOTHING );
	if( RET_IS_OK(r) ) {
		pull_addsourcedistributions(rules, *extradistributions);
		for( rule = rules ; rule != NULL ; rule = rule->next ) {
			assert( !rule->used || rule->distribution != NULL );
		}
	}
	return r;
}

/***************************************************************************
 * step four:                                                              *
 * create pull_targets and pull_sources                                    *
 **************************************************************************/

struct pull_source {
	struct pull_source *next;
	/* NULL, if this is a delete rule */
	struct target *source;
	struct pull_rule *rule;
};
struct pull_target {
	/*@null@*/struct pull_target *next;
	/*@null@*/struct pull_source *sources;
	/*@dependent@*/struct target *target;
	/*@null@*/struct upgradelist *upgradelist;
	/* Ignore delete marks (as some lists were missing) */
	bool_t ignoredelete;
};

static void pull_freetargets(struct pull_target *targets) {
	while( targets != NULL ) {
		struct pull_target *target = targets;
		targets = target->next;
		while( target->sources != NULL ) {
			struct pull_source *source = target->sources;
			target->sources = source->next;
			free(source);
		}
		free(target);
	}
}

static retvalue pull_createsource(struct pull_rule *rule,
		struct target *target,
		struct pull_source ***s) {
	const struct strlist *c;
	const struct strlist *a_from,*a_into;
	int ai;

	assert( rule != NULL );
	assert( rule->distribution != NULL );

	if( rule->architectures_into.count > 0 ) {
		a_from = &rule->architectures_from;
		a_into = &rule->architectures_into;
	} else {
		a_from = &rule->distribution->architectures;
		a_into = &rule->distribution->architectures;
	}
	if( strcmp(target->packagetype,"udeb") == 0 )  {
		if( rule->udebcomponents.count > 0 )
			c = &rule->udebcomponents;
		else
			c = &rule->distribution->udebcomponents;
	} else {
		if( rule->components.count > 0 )
			c = &rule->components;
		else
			c = &rule->distribution->components;
	}

	if( !strlist_in(c, target->component) )
		return RET_NOTHING;

	for( ai = 0 ; ai < a_into->count ; ai++ ) {
		struct pull_source *source;
		if( strcmp(a_into->values[ai],target->architecture) != 0 )
			continue;

		source = malloc(sizeof(struct pull_source));
		if( source == NULL )
			return RET_ERROR_OOM;

		source->next = NULL;
		source->rule = rule;
		source->source = distribution_getpart(rule->distribution,
				target->component, a_from->values[ai],
				target->packagetype);
		**s = source;
		*s = &source->next;
	}
	return RET_OK;
}

static retvalue pull_createdelete(struct pull_source ***s) {
	struct pull_source *source;

	source = malloc(sizeof(struct pull_source));
	if( source == NULL )
		return RET_ERROR_OOM;

	source->next =  NULL;
	source->rule = NULL;
	source->source = NULL;
	**s = source;
	*s = &source->next;
	return RET_OK;
}

static retvalue generatepulltarget(struct pull_distribution *pd, struct target *target) {
	struct pull_source **s;
	struct pull_target *pt;
	retvalue r;
	int i;

	pt = malloc(sizeof(struct pull_target));
	if( pt == NULL ) {
		return RET_ERROR_OOM;
	}
	pt->target = target;
	pt->next = pd->targets;
	pt->upgradelist = NULL;
	pt->ignoredelete = FALSE;
	pt->sources = NULL;
	s = &pt->sources;
	pd->targets = pt;

	for( i = 0 ; i < pd->distribution->pulls.count ; i++ ) {
		struct pull_rule *rule = pd->rules[i];

		if( rule == NULL)
			r = pull_createdelete(&s);
		else
			r = pull_createsource(rule, target, &s);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	return RET_OK;
}

static retvalue pull_generatetargets(struct pull_distribution *pull_distributions) {
	struct pull_distribution *pd;
	struct target *target;
	struct pull_distribution *u_ds;
	retvalue r;

	u_ds = NULL;

	for( pd = pull_distributions ; pd != NULL ; pd = pd->next ) {
		for( target = pd->distribution->targets ; target != NULL ;
				target = target->next) {

			r = generatepulltarget(pd,target);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return RET_OK;
}

/***************************************************************************
 * combination of the steps two, three and four                            *
 **************************************************************************/

retvalue pull_prepare(const char *confdir,struct pull_rule *rules,struct distribution *distributions, struct pull_distribution **pd,struct distribution **alsoneeded) {
	struct distribution *extra;
	struct pull_distribution *pulls;
	retvalue r;

	r = pull_init(&pulls, rules, distributions);
	if( RET_WAS_ERROR(r) )
		return r;

	pull_addsourcedistributions(rules, distributions);

	r = pull_loadmissingsourcedistributions(confdir, rules, &extra);
	if( RET_WAS_ERROR(r) ) {
		pull_freedistributions(pulls);
		return r;
	}
	r = pull_generatetargets(pulls);
	if( RET_WAS_ERROR(r) ) {
		// thats a bit ugly, as rules are already poluted with them...
		distribution_freelist(extra);
		pull_freedistributions(pulls);
		return r;
	}
	*alsoneeded = extra;
	*pd = pulls;
	return RET_OK;
}

/***************************************************************************
 * step five:                                                              *
 * decide what gets pulled                                                 *
 **************************************************************************/

static upgrade_decision ud_decide_by_rule(void *privdata, const char *package,UNUSED(const char *old_version),UNUSED(const char *new_version),const char *newcontrolchunk) {
	struct pull_rule *rule = privdata;
	retvalue r;

	switch( filterlist_find(package,&rule->filterlist) ) {
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

	if( rule->includecondition != NULL ) {
		r = term_decidechunk(rule->includecondition,newcontrolchunk);
		if( RET_WAS_ERROR(r) )
			return UD_ERROR;
		if( r == RET_NOTHING ) {
			return UD_NO;
		}
	}

	return UD_UPGRADE;
}

static inline retvalue pull_searchformissing(const char *dbdir,struct pull_target *p) {
	struct pull_source *source;
	retvalue result,r;

	if( verbose > 2 )
		fprintf(stderr,"  pulling into '%s'\n",p->target->identifier);
	assert(p->upgradelist == NULL);
	r = upgradelist_initialize(&p->upgradelist,p->target,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;

	for( source=p->sources ; source != NULL ; source=source->next ) {

		if( source->rule == NULL ) {
			if( verbose > 4 )
				fprintf(stderr,"  marking everything to be deleted\n");
			r = upgradelist_deleteall(p->upgradelist);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				return result;
			p->ignoredelete = FALSE;
			continue;
		}

		if( verbose > 4 )
			fprintf(stderr,"  looking what to get from '%s'\n",
					source->source->identifier);
		r = upgradelist_pull(p->upgradelist,
				source->source,
				ud_decide_by_rule, source->rule,
				dbdir);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			return result;
	}

	return result;
}

static retvalue pull_search(const char *dbdir,struct pull_distribution *d) {
	retvalue result,r;
	struct pull_target *u;

	if( d->distribution->deb_override != NULL ||
			d->distribution->dsc_override != NULL ||
			d->distribution->udeb_override != NULL ) {
		if( verbose >= 0 )
			fprintf(stderr,
"Warning: Override-Files of '%s' ignored as not yet supported while updating!\n",
					d->distribution->codename);
	}
	if( d->distribution->tracking != dt_NONE ) {
		fprintf(stderr,
"WARNING: Pull does not yet update trackingdata. Trackingdata of %s will be outdated!\n",
					d->distribution->codename);
	}

	result = RET_NOTHING;
	for( u=d->targets ; u != NULL ; u=u->next ) {
		r = pull_searchformissing(dbdir,u);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	return result;
}

static retvalue pull_install(const char *dbdir,filesdb filesdb,references refs,struct pull_distribution *distribution,struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	struct pull_target *u;

	result = RET_NOTHING;
	for( u=distribution->targets ; u != NULL ; u=u->next ) {
		r = upgradelist_install(u->upgradelist,dbdir,filesdb,refs,u->ignoredelete,dereferencedfilekeys);
		RET_UPDATE(distribution->distribution->status, r);
		RET_UPDATE(result,r);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
		if( RET_WAS_ERROR(r) )
			break;
	}
	return result;
}

static void pull_dump(struct pull_distribution *distribution) {
	struct pull_target *u;

	for( u=distribution->targets ; u != NULL ; u=u->next ) {
		if( u->upgradelist == NULL )
			continue;
		printf("Updates needed for '%s':\n",u->target->identifier);
		upgradelist_dump(u->upgradelist);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
	}
}

retvalue pull_update(const char *dbdir,filesdb filesdb,references refs,struct pull_distribution *distributions,struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	struct pull_distribution *d;

	if( verbose >= 0 )
		printf("Calculating packages to pull...\n");

	result = RET_NOTHING;

	for( d=distributions ; d != NULL ; d=d->next) {
		r = pull_search(dbdir,d);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		// TODO: make already here sure the files are ready?
	}
	if( RET_WAS_ERROR(result) ) {
		for( d=distributions ; d != NULL ; d=d->next) {
			struct pull_target *u;
			for( u=d->targets ; u != NULL ; u=u->next ) {
				upgradelist_free(u->upgradelist);
				u->upgradelist = NULL;
			}
		}
		return result;
	}
	if( verbose >= 0 )
		printf("Installing (and possibly deleting) packages...\n");

	for( d=distributions ; d != NULL ; d=d->next) {
		r = pull_install(dbdir,filesdb,refs,d,dereferencedfilekeys);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}

	return result;
}

retvalue pull_checkupdate(const char *dbdir,struct pull_distribution *distributions) {
	struct pull_distribution *d;
	retvalue result,r;

	if( verbose >= 0 )
		fprintf(stderr,"Calculating packages to get...\n");

	result = RET_NOTHING;

	for( d=distributions ; d != NULL ; d=d->next) {
		r = pull_search(dbdir,d);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		pull_dump(d);
	}

	return result;
}

