/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007,2016 Bernhard R. Link
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
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "pull.h"
#include "upgradelist.h"
#include "distribution.h"
#include "tracking.h"
#include "termdecide.h"
#include "filterlist.h"
#include "log.h"
#include "configparser.h"
#include "package.h"

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
	//e.g. "Architectures: i386 sparc mips" (not set means all)
	struct atomlist architectures_from;
	struct atomlist architectures_into;
	bool architectures_set;
	//e.g. "Components: main contrib" (not set means all)
	struct atomlist components;
	bool components_set;
	//e.g. "UDebComponents: main" // (not set means all)
	struct atomlist udebcomponents;
	bool udebcomponents_set;
	// We don't have equivalents for ddebs yet since we don't know
	// what the Debian archive layout is going to look like
	// NULL means no condition
	/*@null@*/term *includecondition;
	struct filterlist filterlist;
	struct filterlist filtersrclist;
	/*----only set after _addsourcedistribution----*/
	/*@NULL@*/ struct distribution *distribution;
	bool used;
};

static void pull_rule_free(/*@only@*/struct pull_rule *pull) {
	if (pull == NULL)
		return;
	free(pull->name);
	free(pull->from);
	atomlist_done(&pull->architectures_from);
	atomlist_done(&pull->architectures_into);
	atomlist_done(&pull->components);
	atomlist_done(&pull->udebcomponents);
	term_free(pull->includecondition);
	filterlist_release(&pull->filterlist);
	filterlist_release(&pull->filtersrclist);
	free(pull);
}

void pull_freerules(struct pull_rule *p) {
	while (p != NULL) {
		struct pull_rule *rule;

		rule = p;
		p = rule->next;
		pull_rule_free(rule);
	}
}

CFlinkedlistinit(pull_rule)
CFvalueSETPROC(pull_rule, name)
CFvalueSETPROC(pull_rule, from)
CFatomlistSETPROC(pull_rule, components, at_component)
CFatomlistSETPROC(pull_rule, udebcomponents, at_component)
CFfilterlistSETPROC(pull_rule, filterlist)
CFfilterlistSETPROC(pull_rule, filtersrclist)
CFtermSETPROC(pull_rule, includecondition)

CFUSETPROC(pull_rule, architectures) {
	CFSETPROCVAR(pull_rule, this);
	retvalue r;

	this->architectures_set = true;
	r = config_getsplitatoms(iter, "Architectures",
			at_architecture,
			&this->architectures_from,
			&this->architectures_into);
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Warning parsing %s, line %u: an empty Architectures field\n"
"causes the whole rule to do nothing.\n",
				config_filename(iter),
				config_markerline(iter));
	}
	return r;
}

static const struct configfield pullconfigfields[] = {
	CFr("Name", pull_rule, name),
	CFr("From", pull_rule, from),
	CF("Architectures", pull_rule, architectures),
	CF("Components", pull_rule, components),
	CF("UDebComponents", pull_rule, udebcomponents),
	CF("FilterFormula", pull_rule, includecondition),
	CF("FilterSrcList", pull_rule, filtersrclist),
	CF("FilterList", pull_rule, filterlist)
};

retvalue pull_getrules(struct pull_rule **rules) {
	struct pull_rule *pull = NULL;
	retvalue r;

	r = configfile_parse("pulls", IGNORABLE(unknownfield),
			configparser_pull_rule_init, linkedlistfinish,
			"pull rule",
			pullconfigfields, ARRAYCOUNT(pullconfigfields), &pull);
	if (RET_IS_OK(r))
		*rules = pull;
	else if (r == RET_NOTHING) {
		assert (pull == NULL);
		*rules = NULL;
		r = RET_OK;
	} else {
		// TODO special handle unknownfield
		pull_freerules(pull);
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
	while (d != NULL) {
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
	if (distribution->pulls.count == 0)
		return RET_NOTHING;

	p = malloc(sizeof(struct pull_distribution)+
		sizeof(struct pull_rules *)*distribution->pulls.count);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	p->next = NULL;
	p->distribution = distribution;
	p->targets = NULL;
	for (i = 0 ; i < distribution->pulls.count ; i++) {
		const char *name = distribution->pulls.values[i];
		if (strcmp(name, "-") == 0) {
			p->rules[i] = NULL;
		} else {
			struct pull_rule *rule = rules;
			while (rule && strcmp(rule->name, name) != 0)
				rule = rule->next;
			if (rule == NULL) {
				fprintf(stderr,
"Error: Unknown pull rule '%s' in distribution '%s'!\n",
						name, distribution->codename);
				free(p);
				return RET_ERROR_MISSING;
			}
			p->rules[i] = rule;
			rule->used = true;
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

	for (d = distributions ; d != NULL ; d = d->next) {
		if (!d->selected)
			continue;
		r = pull_initdistribution(pp, d, rules);
		if (RET_WAS_ERROR(r)) {
			pull_freedistributions(p);
			return r;
		}
		if (RET_IS_OK(r)) {
			assert (*pp != NULL);
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

static retvalue pull_loadsourcedistributions(struct distribution *alldistributions, struct pull_rule *rules) {
	struct pull_rule *rule;
	struct distribution *d;

	for (rule = rules ; rule != NULL ; rule = rule->next) {
		if (rule->used && rule->distribution == NULL) {
			for (d = alldistributions ; d != NULL ; d = d->next) {
				if (strcmp(d->codename, rule->from) == 0) {
					rule->distribution = d;
					break;
				}
			}
			if (d == NULL) {
				fprintf(stderr,
"Error: Unknown distribution '%s' referenced in pull rule '%s'\n",
						rule->from, rule->name);
				return RET_ERROR_MISSING;
			}
		}
	}
	return RET_OK;
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
};

static void pull_freetargets(struct pull_target *targets) {
	while (targets != NULL) {
		struct pull_target *target = targets;
		targets = target->next;
		while (target->sources != NULL) {
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
	const struct atomlist *c;
	const struct atomlist *a_from, *a_into;
	int ai;

	assert (rule != NULL);
	assert (rule->distribution != NULL);

	if (rule->architectures_set) {
		a_from = &rule->architectures_from;
		a_into = &rule->architectures_into;
	} else {
		a_from = &rule->distribution->architectures;
		a_into = &rule->distribution->architectures;
	}
	if (target->packagetype == pt_udeb)  {
		if (rule->udebcomponents_set)
			c = &rule->udebcomponents;
		else
			c = &rule->distribution->udebcomponents;
	} else {
		if (rule->components_set)
			c = &rule->components;
		else
			c = &rule->distribution->components;
	}

	if (!atomlist_in(c, target->component))
		return RET_NOTHING;

	for (ai = 0 ; ai < a_into->count ; ai++) {
		struct pull_source *source;

		if (a_into->atoms[ai] != target->architecture)
			continue;

		source = NEW(struct pull_source);
		if (FAILEDTOALLOC(source))
			return RET_ERROR_OOM;

		source->next = NULL;
		source->rule = rule;
		source->source = distribution_getpart(rule->distribution,
				target->component,
				a_from->atoms[ai],
				target->packagetype);
		**s = source;
		*s = &source->next;
	}
	return RET_OK;
}

static retvalue pull_createdelete(struct pull_source ***s) {
	struct pull_source *source;

	source = NEW(struct pull_source);
	if (FAILEDTOALLOC(source))
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

	pt = NEW(struct pull_target);
	if (FAILEDTOALLOC(pt))
		return RET_ERROR_OOM;
	pt->target = target;
	pt->next = pd->targets;
	pt->upgradelist = NULL;
	pt->sources = NULL;
	s = &pt->sources;
	pd->targets = pt;

	for (i = 0 ; i < pd->distribution->pulls.count ; i++) {
		struct pull_rule *rule = pd->rules[i];

		if (rule == NULL)
			r = pull_createdelete(&s);
		else
			r = pull_createsource(rule, target, &s);
		if (RET_WAS_ERROR(r))
			return r;
	}

	return RET_OK;
}

static retvalue pull_generatetargets(struct pull_distribution *pull_distributions, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes) {
	struct pull_distribution *pd;
	struct target *target;
	retvalue r;

	for (pd = pull_distributions ; pd != NULL ; pd = pd->next) {
		for (target = pd->distribution->targets ; target != NULL ;
				target = target->next) {

			if (!target_matches(target, components, architectures, packagetypes))
				continue;

			r = generatepulltarget(pd, target);
			if (RET_WAS_ERROR(r))
				return r;
		}
	}
	return RET_OK;
}

/***************************************************************************
 * Some checking to be able to warn against typos                          *
 **************************************************************************/

static bool *preparefoundlist(const struct atomlist *list) {
	bool *found;
	int i, j;

	found = nzNEW(list->count, bool);
	if (FAILEDTOALLOC(found))
		return found;
	for (i = 0 ; i < list->count ; i++) {
		if (found[i])
			continue;
		for (j = i + 1 ; j < list->count ; j++)
			if (list->atoms[i] == list->atoms[j])
				found[j] = true;
	}
	return found;
}


static inline void markasused(const struct strlist *pulls, const char *rulename, const struct atomlist *needed, const struct atomlist *have, bool *found) {
	int i, j, o;

	for (i = 0 ; i < pulls->count ; i++) {
		if (strcmp(pulls->values[i], rulename) != 0)
			continue;
		for (j = 0 ; j < have->count ; j++) {
			o = atomlist_ofs(needed, have->atoms[j]);
			if (o >= 0)
				found[o] = true;
		}
	}
}

static void checkifarchitectureisused(const struct atomlist *architectures, const struct distribution *alldistributions, const struct pull_rule *rule, const char *action) {
	bool *found;
	const struct distribution *d;
	int i;

	assert (rule != NULL);
	if (architectures->count == 0)
		return;
	found = preparefoundlist(architectures);
	if (found == NULL)
		return;
	for (d = alldistributions ; d != NULL ; d = d->next) {
		markasused(&d->pulls, rule->name,
				architectures, &d->architectures,
				found);
	}
	for (i = 0 ; i < architectures->count ; i++) {
		if (found[i])
			continue;
		fprintf(stderr,
"Warning: pull rule '%s' wants to %s architecture '%s',\n"
"but no distribution using this has such an architecture.\n"
"(This will simply be ignored and is not even checked when using --fast).\n",
				rule->name, action,
				atoms_architectures[architectures->atoms[i]]);
	}
	free(found);
	return;
}

static void checkifcomponentisused(const struct atomlist *components, const struct distribution *alldistributions, const struct pull_rule *rule, const char *action) {
	bool *found;
	const struct distribution *d;
	int i;

	assert (rule != NULL);
	if (components->count == 0)
		return;
	found = preparefoundlist(components);
	if (found == NULL)
		return;
	for (d = alldistributions ; d != NULL ; d = d->next) {
		markasused(&d->pulls, rule->name,
				components, &d->components,
				found);
	}
	for (i = 0 ; i < components->count ; i++) {
		if (found[i])
			continue;
		fprintf(stderr,
"Warning: pull rule '%s' wants to %s component '%s',\n"
"but no distribution using this has such an component.\n"
"(This will simply be ignored and is not even checked when using --fast).\n",
				rule->name, action,
				atoms_components[components->atoms[i]]);
	}
	free(found);
	return;
}

static void checkifudebcomponentisused(const struct atomlist *udebcomponents, const struct distribution *alldistributions, const struct pull_rule *rule, const char *action) {
	bool *found;
	const struct distribution *d;
	int i;

	assert (rule != NULL);
	if (udebcomponents->count == 0)
		return;
	found = preparefoundlist(udebcomponents);
	if (found == NULL)
		return;
	for (d = alldistributions ; d != NULL ; d = d->next) {
		markasused(&d->pulls, rule->name,
				udebcomponents, &d->udebcomponents,
				found);
	}
	for (i = 0 ; i < udebcomponents->count ; i++) {
		if (found[i])
			continue;
		fprintf(stderr,
"Warning: pull rule '%s' wants to %s udeb component '%s',\n"
"but no distribution using this has such an udeb component.\n"
"(This will simply be ignored and is not even checked when using --fast).\n",
				rule->name, action,
				atoms_components[udebcomponents->atoms[i]]);
	}
	free(found);
	return;
}

static void checksubset(const struct atomlist *needed, const struct atomlist *have, const char *rulename, const char *from, const char *what, const char **atoms) {
	int i, j;

	for (i = 0 ; i < needed->count ; i++) {
		atom_t value = needed->atoms[i];

		for (j = 0 ; j < i ; j++) {
			if (value == needed->atoms[j])
				break;
		}
		if (j < i)
			continue;

		if (!atomlist_in(have, value)) {
			fprintf(stderr,
"Warning: pull rule '%s' wants to get something from %s '%s',\n"
"but there is no such %s in distribution '%s'.\n"
"(This will simply be ignored and is not even checked when using --fast).\n",
					rulename, what,
					atoms[value], what, from);
		}
	}
}

static void searchunused(const struct distribution *alldistributions, const struct pull_rule *rule) {
	if (rule->distribution != NULL) {
		// TODO: move this part of the checks into parsing?
		checksubset(&rule->architectures_from,
				&rule->distribution->architectures,
				rule->name, rule->from, "architecture",
				atoms_architectures);
		checksubset(&rule->components,
				&rule->distribution->components,
				rule->name, rule->from, "component",
				atoms_components);
		checksubset(&rule->udebcomponents,
				&rule->distribution->udebcomponents,
				rule->name, rule->from, "udeb component",
				atoms_components);
	}

	if (rule->distribution == NULL) {
		assert (strcmp(rule->from, "*") == 0);
		checkifarchitectureisused(&rule->architectures_from,
				alldistributions, rule, "get something from");
		/* no need to check component and udebcomponent, as those
		 * are the same with the others */
	}
	checkifarchitectureisused(&rule->architectures_into,
			alldistributions, rule, "put something into");
	checkifcomponentisused(&rule->components,
			alldistributions, rule, "put something into");
	checkifudebcomponentisused(&rule->udebcomponents,
			alldistributions, rule, "put something into");
}

static void pull_searchunused(const struct distribution *alldistributions, struct pull_rule *pull_rules) {
	struct pull_rule *rule;

	for (rule = pull_rules ; rule != NULL ; rule = rule->next) {
		if (!rule->used)
			continue;

		searchunused(alldistributions, rule);
	}
}

/***************************************************************************
 * combination of the steps two, three and four                            *
 **************************************************************************/

retvalue pull_prepare(struct distribution *alldistributions, struct pull_rule *rules, bool fast, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *types, struct pull_distribution **pd) {
	struct pull_distribution *pulls;
	retvalue r;

	r = pull_init(&pulls, rules, alldistributions);
	if (RET_WAS_ERROR(r))
		return r;

	r = pull_loadsourcedistributions(alldistributions, rules);
	if (RET_WAS_ERROR(r)) {
		pull_freedistributions(pulls);
		return r;
	}
	if (!fast)
		pull_searchunused(alldistributions, rules);

	r = pull_generatetargets(pulls, components, architectures, types);
	if (RET_WAS_ERROR(r)) {
		pull_freedistributions(pulls);
		return r;
	}
	*pd = pulls;
	return RET_OK;
}

/***************************************************************************
 * step five:                                                              *
 * decide what gets pulled                                                 *
 **************************************************************************/

static upgrade_decision ud_decide_by_rule(void *privdata, struct target *target, struct package *new, /*@null@*/const char *old_version) {
	struct pull_rule *rule = privdata;
	upgrade_decision decision = UD_UPGRADE;
	retvalue r;
	struct filterlist *fl;
	const char *n, *v;
	bool cmdline_still_undecided;

	if (target->packagetype == pt_dsc) {
		assert (strcmp(new->name, new->source) == 0);
		assert (strcmp(new->version, new->sourceversion) == 0);
		if (rule->filtersrclist.set)
			fl = &rule->filtersrclist;
		else
			fl = &rule->filterlist;
		n = new->name;
		v = new->version;
	} else {
		if (rule->filterlist.set) {
			fl = &rule->filterlist;
			n = new->name;
			v = new->version;
		} else {
			fl = &rule->filtersrclist;
			n = new->source;
			v = new->sourceversion;
		}
	}

	switch (filterlist_find(n, v, fl)) {
		case flt_deinstall:
		case flt_purge:
			return UD_NO;
		case flt_warning:
			return UD_LOUDNO;
		case flt_supersede:
			decision = UD_SUPERSEDE;
			break;
		case flt_hold:
			decision = UD_HOLD;
			break;
		case flt_error:
			/* cannot yet be handled! */
			fprintf(stderr,
"Package name marked to be unexpected('error'): '%s'!\n", new->name);
			return UD_ERROR;
		case flt_upgradeonly:
			if (old_version == NULL)
				return UD_NO;
			break;
		case flt_install:
			break;
		case flt_unchanged:
		case flt_auto_hold:
			assert (false);
			break;
	}

	cmdline_still_undecided = false;
	switch (filterlist_find(new->source, new->sourceversion,
				&cmdline_src_filter)) {
		case flt_deinstall:
		case flt_purge:
			return UD_NO;
		case flt_warning:
			return UD_LOUDNO;
		case flt_auto_hold:
			cmdline_still_undecided = true;
			decision = UD_HOLD;
			break;
		case flt_hold:
			decision = UD_HOLD;
			break;
		case flt_supersede:
			decision = UD_SUPERSEDE;
			break;
		case flt_error:
			/* cannot yet be handled! */
			fprintf(stderr,
"Package name marked to be unexpected('error'): '%s'!\n", new->name);
			return UD_ERROR;
		case flt_upgradeonly:
			if (old_version == NULL)
				return UD_NO;
			break;
		case flt_install:
			decision = UD_UPGRADE;
			break;
		case flt_unchanged:
			cmdline_still_undecided = true;
			break;
	}


	if (target->packagetype != pt_dsc) {
		switch (filterlist_find(new->name, new->version,
					&cmdline_bin_filter)) {
			case flt_deinstall:
			case flt_purge:
				return UD_NO;
			case flt_warning:
				return UD_LOUDNO;
			case flt_hold:
				decision = UD_HOLD;
				break;
			case flt_supersede:
				decision = UD_SUPERSEDE;
				break;
			case flt_error:
				/* cannot yet be handled! */
				fprintf(stderr,
"Package name marked to be unexpected('error'): '%s'!\n", new->name);
				return UD_ERROR;
			case flt_upgradeonly:
				if (old_version == NULL)
					return UD_NO;
				break;
			case flt_install:
				decision = UD_UPGRADE;
				break;
			case flt_unchanged:
				break;
			case flt_auto_hold:
				/* hold only if it was not in the src-filter */
				if (cmdline_still_undecided)
					decision = UD_HOLD;
				break;
		}
	} else if (cmdline_bin_filter.defaulttype == flt_auto_hold) {
		if (cmdline_still_undecided)
			decision = UD_HOLD;
	}

	/* formula tested last as it is the most expensive */
	if (rule->includecondition != NULL) {
		r = term_decidepackage(rule->includecondition, new, target);
		if (RET_WAS_ERROR(r))
			return UD_ERROR;
		if (r == RET_NOTHING) {
			return UD_NO;
		}
	}

	return decision;
}

static inline retvalue pull_searchformissing(/*@null@*/FILE *out, struct pull_target *p) {
	struct pull_source *source;
	retvalue result, r;

	if (verbose > 2 && out != NULL)
		fprintf(out, "  pulling into '%s'\n", p->target->identifier);
	assert(p->upgradelist == NULL);
	r = upgradelist_initialize(&p->upgradelist, p->target);
	if (RET_WAS_ERROR(r))
		return r;

	result = RET_NOTHING;

	for (source=p->sources ; source != NULL ; source=source->next) {

		if (source->rule == NULL) {
			if (verbose > 4 && out != NULL)
				fprintf(out,
"  marking everything to be deleted\n");
			r = upgradelist_deleteall(p->upgradelist);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				return result;
			continue;
		}

		if (verbose > 4 && out != NULL)
			fprintf(out, "  looking what to get from '%s'\n",
					source->source->identifier);
		r = upgradelist_pull(p->upgradelist, source->source,
				ud_decide_by_rule, source->rule, source);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			return result;
	}

	return result;
}

static retvalue pull_search(/*@null@*/FILE *out, struct pull_distribution *d) {
	retvalue result, r;
	struct pull_target *u;

	result = RET_NOTHING;
	for (u=d->targets ; u != NULL ; u=u->next) {
		r = pull_searchformissing(out, u);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}
	return result;
}

static bool pull_isbigdelete(struct pull_distribution *d) {
	struct pull_target *u, *v;

	for (u = d->targets ; u != NULL ; u=u->next) {
		if (upgradelist_isbigdelete(u->upgradelist)) {
			d->distribution->omitted = true;
			for (v = d->targets ; v != NULL ; v = v->next) {
				upgradelist_free(v->upgradelist);
				v->upgradelist = NULL;
			}
			return true;
		}
	}
	return false;
}


static void pull_from_callback(void *privdata, const char **rule_p, const char **from_p) {
	struct pull_source *source = privdata;

	*rule_p = source->rule->name;
	*from_p = source->rule->from;
}

static retvalue pull_install(struct pull_distribution *distribution) {
	retvalue result, r;
	struct pull_target *u;
	struct distribution *d = distribution->distribution;

	assert (logger_isprepared(d->logger));

	result = RET_NOTHING;
	for (u=distribution->targets ; u != NULL ; u=u->next) {
		r = upgradelist_install(u->upgradelist, d->logger,
				false, pull_from_callback);
		RET_UPDATE(d->status, r);
		RET_UPDATE(result, r);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
		if (RET_WAS_ERROR(r))
			break;
	}
	if (RET_IS_OK(result) && d->tracking != dt_NONE) {
		r = tracking_retrack(d, false);
		RET_ENDUPDATE(result, r);
	}
	return result;
}

static void pull_dumppackage(const char *packagename, /*@null@*/const char *oldversion, /*@null@*/const char *newversion, /*@null@*/const char *bestcandidate, /*@null@*/const struct strlist *newfilekeys, /*@null@*/const char *newcontrol, void *privdata) {
	struct pull_source *source = privdata;

	if (newversion == NULL) {
		if (oldversion != NULL && bestcandidate != NULL) {
			printf("'%s': '%s' will be deleted"
					" (best new: '%s')\n",
					packagename, oldversion, bestcandidate);
		} else if (oldversion != NULL) {
			printf("'%s': '%s' will be deleted"
					" (no longer available or superseded)\n",
					packagename, oldversion);
		} else {
			printf("'%s': will NOT be added as '%s'\n",
					packagename, bestcandidate);
		}
	} else if (newversion == oldversion) {
		if (bestcandidate != NULL) {
			if (verbose > 1)
				printf("'%s': '%s' will be kept"
						" (best new: '%s')\n",
						packagename, oldversion,
						bestcandidate);
		} else {
			if (verbose > 0)
				printf("'%s': '%s' will be kept"
						" (unavailable for reload)\n",
						packagename, oldversion);
		}
	} else {
		const char *via = source->rule->name;

		assert (newfilekeys != NULL);
		assert (newcontrol != NULL);
		if (oldversion != NULL)
			(void)printf("'%s': '%s' will be upgraded"
					" to '%s' (from '%s'):\n files needed: ",
					packagename, oldversion,
					newversion, via);
		else
			(void)printf("'%s': newly installed"
					" as '%s' (from '%s'):\n files needed: ",
					packagename, newversion, via);
		(void)strlist_fprint(stdout, newfilekeys);
		if (verbose > 2)
			(void)printf("\n installing as: '%s'\n",
					newcontrol);
		else
			(void)putchar('\n');
	}
}

static void pull_dump(struct pull_distribution *distribution) {
	struct pull_target *u;

	for (u=distribution->targets ; u != NULL ; u=u->next) {
		if (u->upgradelist == NULL)
			continue;
		printf("Updates needed for '%s':\n", u->target->identifier);
		upgradelist_dump(u->upgradelist, pull_dumppackage);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
	}
}

static void pull_dumplistpackage(const char *packagename, /*@null@*/const char *oldversion, /*@null@*/const char *newversion, /*@null@*/const char *bestcandidate, /*@null@*/const struct strlist *newfilekeys, /*@null@*/const char *newcontrol, void *privdata) {
	struct pull_source *source = privdata;

	if (newversion == NULL) {
		if (oldversion == NULL)
			return;
		printf("delete '%s' '%s'\n", packagename, oldversion);
	} else if (newversion == oldversion) {
		if (bestcandidate != NULL)
			printf("keep '%s' '%s' '%s'\n", packagename,
					oldversion, bestcandidate);
		else
			printf("keep '%s' '%s' unavailable\n", packagename,
					oldversion);
	} else {
		const char *via = source->rule->name;

		assert (newfilekeys != NULL);
		assert (newcontrol != NULL);
		if (oldversion != NULL)
			(void)printf("update '%s' '%s' '%s' '%s'\n",
					packagename, oldversion,
					newversion, via);
		else
			(void)printf("add '%s' - '%s' '%s'\n",
					packagename, newversion, via);
	}
}

static void pull_dumplist(struct pull_distribution *distribution) {
	struct pull_target *u;

	for (u=distribution->targets ; u != NULL ; u=u->next) {
		if (u->upgradelist == NULL)
			continue;
		printf("Updates needed for '%s':\n", u->target->identifier);
		upgradelist_dump(u->upgradelist, pull_dumplistpackage);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
	}
}

retvalue pull_update(struct pull_distribution *distributions) {
	retvalue result, r;
	struct pull_distribution *d;

	for (d=distributions ; d != NULL ; d=d->next) {
		r = distribution_prepareforwriting(d->distribution);
		if (RET_WAS_ERROR(r))
			return r;
		r = distribution_loadalloverrides(d->distribution);
		if (RET_WAS_ERROR(r))
			return r;
	}

	if (verbose >= 0)
		printf("Calculating packages to pull...\n");

	result = RET_NOTHING;

	for (d=distributions ; d != NULL ; d=d->next) {
		r = pull_search(stdout, d);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		// TODO: make already here sure the files are ready?
	}
	if (RET_WAS_ERROR(result)) {
		for (d=distributions ; d != NULL ; d=d->next) {
			struct pull_target *u;
			for (u=d->targets ; u != NULL ; u=u->next) {
				upgradelist_free(u->upgradelist);
				u->upgradelist = NULL;
			}
		}
		return result;
	}
	if (verbose >= 0)
		printf("Installing (and possibly deleting) packages...\n");

	for (d=distributions ; d != NULL ; d=d->next) {
		if (global.onlysmalldeletes) {
			if (pull_isbigdelete(d)) {
				fprintf(stderr,
"Not processing '%s' because of --onlysmalldeletes\n",
						d->distribution->codename);
				continue;
			}
		}
		r = pull_install(d);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}
	logger_wait();

	return result;
}

retvalue pull_checkupdate(struct pull_distribution *distributions) {
	struct pull_distribution *d;
	retvalue result, r;

	for (d=distributions ; d != NULL ; d=d->next) {
		r = distribution_loadalloverrides(d->distribution);
		if (RET_WAS_ERROR(r))
			return r;
	}

	if (verbose >= 0)
		fprintf(stderr, "Calculating packages to get...\n");

	result = RET_NOTHING;

	for (d=distributions ; d != NULL ; d=d->next) {
		r = pull_search(stderr, d);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		pull_dump(d);
	}

	return result;
}

retvalue pull_dumpupdate(struct pull_distribution *distributions) {
	struct pull_distribution *d;
	retvalue result, r;

	for (d=distributions ; d != NULL ; d=d->next) {
		r = distribution_loadalloverrides(d->distribution);
		if (RET_WAS_ERROR(r))
			return r;
	}

	result = RET_NOTHING;

	for (d=distributions ; d != NULL ; d=d->next) {
		r = pull_search(NULL, d);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		pull_dumplist(d);
	}

	return result;
}
