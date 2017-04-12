/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2008,2009,2016 Bernhard R. Link
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

/* This module handles the updating of distributions from remote repositories.
 * It's using apt's methods (the files in /usr/lib/apt/methods) for the
 * actuall getting of needed lists and package files.
 *
 * It's only task is to request the right actions in the right order,
 * almost everything is done in other modules:
 *
 *  aptmethod.c		start, feed and take care of the apt methods
 *  downloadcache.c     keep track of what is downloaded to avoid duplicates
 *  signature.c		verify Release.gpg files, if requested
 *  remoterepository.c  cache remote index files and decide which to download
 *  upgradelist.c	decide which packages (and version) should be installed
 *
 * An update run consists of the following steps, in between done some
 * downloading, checking and so on:
 *
 * Step  1: parsing the conf/updates file with the patterns
 * Step  2: create rules for some distribution based on those patterns
 * Step  3: calculate which remote indices are to be retrieved and processed
 * Step  4: <removed>
 * Step  5: preperations for actually doing anything
 * Step  6: queue downloading of list of lists (Release, Release.gpg, ...)
 * Step  7: queue downloading of lists (Packages.gz, Sources.gz, ...)
 * Step  8: call possible list hooks allowing them to modify the lists
 * Step  9: search for missing packages i.e. needing to be added or upgraded
 * Step 10: enqueue downloading of missing packages
 * Step 11: install the missing packages
 * Step 12: remember processed index files as processed
 *
 */
#include <config.h>

#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "atoms.h"
#include "dirs.h"
#include "names.h"
#include "signature.h"
#include "aptmethod.h"
#include "downloadcache.h"
#include "updates.h"
#include "upgradelist.h"
#include "distribution.h"
#include "tracking.h"
#include "termdecide.h"
#include "chunks.h"
#include "filterlist.h"
#include "log.h"
#include "donefile.h"
#include "freespace.h"
#include "configparser.h"
#include "filecntl.h"
#include "remoterepository.h"
#include "uncompression.h"
#include "package.h"

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
 u_distribution --> u_origin -> u_origin --> NULL
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
 u_distribution ---> u_origin -> u_origin -> NULL
      |   |            / \          / \
      |  \ /            |            |
      | u_target --> u_index ---> u_index -> NULL
      |   |
      |  \ /
      |  NULL    omitted in this image:
      |          not every target must have an index in each
     \ /         origin. (Some origin might only support a
     NULL        limited number of architectures or components)

                 also omitted are delete rules, i.e. markers
		 that all versions previously found are not to
		 be kept or even installed, unless a later
		 index again adds them.
*/

/* the data for some upstream part to get updates from, some
 * some fields can be NULL or empty */
struct update_pattern {
	struct update_pattern *next;
	//e.g. "Name: woody"
	char *name;
	/* another pattern to take value from */
	char *from;
	/*@dependent@*/struct update_pattern *pattern_from;
	//e.g. "Method: ftp://ftp.uni-freiburg.de/pub/linux/debian"
	/*@null@*/ char *method;
	//e.g. "Fallback: ftp://ftp.debian.org/pub/linux/debian"
	/*@null@*/ char *fallback; // can be other server or dir, but must be same method
	//e.g. "Config: Dir=/"
	struct strlist config;
	//e.g. "Suite: woody" or "Suite: <asterix>/updates" (NULL means "*")
	/*@null@*/char *suite_from;
	//e.g. "VerifyRelease: B629A24C38C6029A" (NULL means not check)
	/*@null@*/char *verifyrelease;
	//e.g. "Architectures: i386 sparc mips" (not set means all)
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
	// There's no ddeb support here yet, since we don't know what the
	// Debian archive layout is going to look like.

	// NULL means no condition
	/*@null@*/term *includecondition;
	struct filterlist filterlist;
	struct filterlist filtersrclist;
	// NULL means nothing to execute after lists are downloaded...
	/*@null@*/char *listhook;
	/*@null@*/char *shellhook;
	/* checksums to not read check in Release file: */
	bool ignorehashes[cs_hashCOUNT];
	/* the name of the flat component, causing flat mode if non-NULL*/
	component_t flat;
	//e.g. "IgnoreRelease: Yes" for 1 (default is 0)
	bool ignorerelease;
	//e.g. "GetInRelease: No" for 0 (default is 1)
	bool getinrelease;
	/* the form in which index files are preferably downloaded */
	struct encoding_preferences downloadlistsas;
	/* if true ignore sources with Extra-Source-Only */
	bool omitextrasource;
	/* if the specific field is there (to destinguish from an empty one) */
	bool omitextrasource_set;
	bool ignorehashes_set;
	bool ignorerelease_set;
	bool getinrelease_set;
	bool architectures_set;
	bool components_set;
	bool udebcomponents_set;
	bool includecondition_set;
	bool config_set;
	bool downloadlistsas_set;
	/* to check circular references */
	bool visited;

	bool used;
	struct remote_repository *repository;
};

struct update_origin {
	struct update_origin *next;
	/* all following are NULL when this is a delete rule */
	/*@null@*/const struct update_pattern *pattern;
	/*@null@*/char *suite_from;
	/*@null@*/const struct distribution *distribution;
	/*@null@*/struct remote_distribution *from;
	/* cache for flat mode */
	bool flat;
	/* set when there was a error and it should no longer be used */
	bool failed;
};

struct update_index_connector {
	struct update_index_connector *next;

	/* NULL when this is a delete rule */
	/*@null@*/ struct remote_index *remote;
	/*@null@*/ struct update_origin *origin;

	/*@null@*/char *afterhookfilename;

	/* ignore wrong architecture packages (arch1>arch2 or flat) */
	bool ignorewrongarchitecture;
	/* if newly downloaded or not in done file */
	bool new;
	/* content needed (i.e. listhooks have to be run) */
	bool needed;
	/* there was something missed here */
	bool failed;
	/* do not generate 'done' file */
	bool incomplete;
};

struct update_target {
	/*@null@*/struct update_target *next;
	/*@null@*/struct update_index_connector *indices;
	/*@dependent@*/struct target *target;
	/*@null@*/struct upgradelist *upgradelist;
	/* Ignore delete marks (as some lists were missing) */
	bool ignoredelete;
	/* don't do anything because of --skipold */
	bool nothingnew;
	/* if true do not generate donefiles */
	bool incomplete;
};

struct update_distribution {
	struct update_distribution *next;
	struct distribution *distribution;
	struct update_pattern **patterns;
	struct update_origin *origins;
	struct update_target *targets;
};

static void update_pattern_free(/*@only@*/struct update_pattern *update) {
	if (update == NULL)
		return;
	free(update->name);
	free(update->from);
	free(update->method);
	free(update->fallback);
	free(update->suite_from);
	free(update->verifyrelease);
	strlist_done(&update->config);
	strlist_done(&update->architectures_from);
	strlist_done(&update->architectures_into);
	strlist_done(&update->components_from);
	strlist_done(&update->components_into);
	strlist_done(&update->udebcomponents_from);
	strlist_done(&update->udebcomponents_into);
	term_free(update->includecondition);
	filterlist_release(&update->filterlist);
	filterlist_release(&update->filtersrclist);
	free(update->listhook);
	free(update->shellhook);
	remote_repository_free(update->repository);
	free(update);
}

void updates_freepatterns(struct update_pattern *p) {
	while (p != NULL) {
		struct update_pattern *pattern;

		pattern = p;
		p = pattern->next;
		update_pattern_free(pattern);
	}
}

static void updates_freeorigins(/*@only@*/struct update_origin *o) {
	while (o != NULL) {
		struct update_origin *origin;

		origin = o;
		o = origin->next;
		free(origin->suite_from);
		free(origin);
	}
}

static void updates_freetargets(/*@only@*/struct update_target *t) {
	while (t != NULL) {
		struct update_target *ut;

		ut = t;
		t = ut->next;
		while (ut->indices != NULL) {
			struct update_index_connector *ui;

			ui = ut->indices;
			ut->indices = ui->next;
			free(ui->afterhookfilename);
			free(ui);
		}
		free(ut);
	}
}

void updates_freeupdatedistributions(struct update_distribution *d) {
	while (d != NULL) {
		struct update_distribution *next;

		next = d->next;
		free(d->patterns);
		updates_freetargets(d->targets);
		updates_freeorigins(d->origins);
		free(d);
		d = next;
	}
}

static inline retvalue newupdatetarget(struct update_target **ts, /*@dependent@*/struct target *target) {
	struct update_target *ut;

	ut = malloc(sizeof(struct update_target));
	if (FAILEDTOALLOC(ut))
		return RET_ERROR_OOM;
	ut->target = target;
	ut->next = *ts;
	ut->indices = NULL;
	ut->upgradelist = NULL;
	ut->ignoredelete = false;
	ut->nothingnew = false;
	ut->incomplete = false;
	*ts = ut;
	return RET_OK;
}

/****************************************************************************
 * Step 1: parsing the conf/updates file with the patterns                  *
 ****************************************************************************/

CFlinkedlistinit(update_pattern)
CFvalueSETPROC(update_pattern, name)
CFvalueSETPROC(update_pattern, suite_from)
CFatomSETPROC(update_pattern, flat, at_component)
CFvalueSETPROC(update_pattern, from)
CFurlSETPROC(update_pattern, method)
CFurlSETPROC(update_pattern, fallback)
/* what here? */
CFallSETPROC(update_pattern, verifyrelease)
CFlinelistSETPROC(update_pattern, config)
CFtruthSETPROC(update_pattern, ignorerelease)
CFtruthSETPROC(update_pattern, getinrelease)
CFscriptSETPROC(update_pattern, listhook)
CFallSETPROC(update_pattern, shellhook)
CFfilterlistSETPROC(update_pattern, filterlist)
CFfilterlistSETPROC(update_pattern, filtersrclist)
CFtermSSETPROC(update_pattern, includecondition)
CFtruthSETPROC(update_pattern, omitextrasource)

CFUSETPROC(update_pattern, downloadlistsas) {
	CFSETPROCVAR(update_pattern, this);
	char *word;
	const char *u;
	retvalue r;
	unsigned int e = 0;
	enum compression c;

	this->downloadlistsas_set = true;
	r = config_getword(iter, &word);
	while (RET_IS_OK(r)) {
		bool force;
		if (e >= ARRAYCOUNT(this->downloadlistsas.requested)) {
			fprintf(stderr,
"%s:%d:%d: Ignoring all but first %d entries...\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter),
					(int)(ARRAYCOUNT(
					  this->downloadlistsas.requested)));
			free(word);
			break;
		}
		if (strncmp(word, "force.", 6) == 0) {
			u = word + 5;
			force = true;
		} else {
			u = word;
			force = false;
		}
		for (c = 0 ; c < c_COUNT ; c++) {
			if (strcmp(uncompression_config[c], u) == 0 ||
			    strcmp(uncompression_config[c]+1, u) == 0) {
				break;
			}
		}
		if (c < c_COUNT) {
			this->downloadlistsas.requested[e].compression = c;
			this->downloadlistsas.requested[e].diff = false;
			this->downloadlistsas.requested[e].force = force;
			e++;
			free(word);
			r = config_getword(iter, &word);
			continue;
		}
		if (strcmp(u, ".diff") == 0 || strcmp(u, "diff") == 0) {
			this->downloadlistsas.requested[e].compression = c_gzip;
			this->downloadlistsas.requested[e].diff = true;
			this->downloadlistsas.requested[e].force = force;
			e++;
			free(word);
			r = config_getword(iter, &word);
			continue;
		}
		fprintf(stderr,
"%s:%d:%d: Error: unknown list download mode '%s'!\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter),
					u);
		free(word);
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	this->downloadlistsas.count = e;
	return RET_OK;
}

CFUSETPROC(update_pattern, components) {
	CFSETPROCVAR(update_pattern, this);
	retvalue r;
	int i;

	this->components_set = true;
	r = config_getsplitwords(iter, "Components",
			&this->components_from,
			&this->components_into);
	if (RET_IS_OK(r)) {
		// TODO: instead of this save numbers directly...
		for (i = 0 ; i < this->components_into.count ; i++) {
			component_t c;
			c = component_find(this->components_into.values[i]);
			if (c == atom_unknown) {
				fprintf(stderr,
"Warning parsing %s, line %u: unknown component '%s' will be ignored!\n",
					config_filename(iter),
					config_markerline(iter),
					this->components_into.values[i]);
			}
		}
	}
	return r;
}

CFUSETPROC(update_pattern, udebcomponents) {
	CFSETPROCVAR(update_pattern, this);
	retvalue r;
	int i;

	this->udebcomponents_set = true;
	r = config_getsplitwords(iter, "UdebComponents",
			&this->udebcomponents_from,
			&this->udebcomponents_into);
	if (RET_IS_OK(r)) {
		// TODO: instead of this save numbers directly...
		for (i = 0 ; i < this->udebcomponents_into.count ; i++) {
			component_t c;
			c = component_find(this->udebcomponents_into.values[i]);
			if (c == atom_unknown) {
				fprintf(stderr,
"Warning parsing %s, line %u: unknown udeb component '%s' will be ignored!\n",
					config_filename(iter),
					config_markerline(iter),
					this->udebcomponents_into.values[i]);
			}
		}
	}
	return r;
}

CFUSETPROC(update_pattern, architectures) {
	CFSETPROCVAR(update_pattern, this);
	retvalue r;
	int i;

	this->architectures_set = true;
	r = config_getsplitwords(iter, "Architectures",
			&this->architectures_from,
			&this->architectures_into);
	if (r == RET_NOTHING) {
		strlist_init(&this->architectures_from);
		strlist_init(&this->architectures_into);
		fprintf(stderr,
"Warning parsing %s, line %u: an empty Architectures field\n"
"causes the whole pattern to do nothing.\n",
				config_filename(iter),
				config_markerline(iter));
	}
	if (RET_IS_OK(r)) {
		// TODO: instead of this save numbers directly...
		for (i = 0 ; i < this->architectures_into.count ; i++) {
			architecture_t a;
			a = architecture_find(this->architectures_into.values[i]);
			if (a == atom_unknown) {
				fprintf(stderr,
"Warning parsing %s, line %u: unknown architecture '%s' will be ignored!\n",
					config_filename(iter),
					config_markerline(iter),
					this->architectures_into.values[i]);
			}
		}
	}
	return r;
}
CFhashesSETPROC(update_pattern, ignorehashes);

static const struct configfield updateconfigfields[] = {
	CFr("Name", update_pattern, name),
	CF("From", update_pattern, from),
	CF("Method", update_pattern, method),
	CF("Fallback", update_pattern, fallback),
	CF("Config", update_pattern, config),
	CF("Suite", update_pattern, suite_from),
	CF("Architectures", update_pattern, architectures),
	CF("Components", update_pattern, components),
	CF("Flat", update_pattern, flat),
	CF("UDebComponents", update_pattern, udebcomponents),
	CF("GetInRelease", update_pattern, getinrelease),
	CF("IgnoreRelease", update_pattern, ignorerelease),
	CF("IgnoreHashes", update_pattern, ignorehashes),
	CF("VerifyRelease", update_pattern, verifyrelease),
	CF("ListHook", update_pattern, listhook),
	CF("ListShellHook", update_pattern, shellhook),
	CF("FilterFormula", update_pattern, includecondition),
	CF("OmitExtraSourceOnly", update_pattern, omitextrasource),
	CF("FilterList", update_pattern, filterlist),
	CF("FilterSrcList", update_pattern, filtersrclist),
	CF("DownloadListsAs", update_pattern, downloadlistsas)
};

CFfinishparse(update_pattern) {
	CFUfinishparseVARS(update_pattern, n, last_p, mydata);

	if (complete) {
		if (n->components_set && atom_defined(n->flat)) {
			fprintf(stderr,
"%s:%u to %u: Update pattern may not contain Components and Flat fields ad the same time.\n",
				config_filename(iter), config_firstline(iter),
				config_line(iter));
			return RET_ERROR;
		}
		if (n->udebcomponents_set && atom_defined(n->flat)) {
			fprintf(stderr,
"%s:%u to %u: Update pattern may not contain UDebComponents and Flat fields ad the same time.\n",
				config_filename(iter), config_firstline(iter),
				config_line(iter));
			return RET_ERROR;
		}
		if (n->from != NULL && n->method != NULL) {
			fprintf(stderr,
"%s:%u to %u: Update pattern may not contain From: and Method: fields ad the same time.\n",
				config_filename(iter), config_firstline(iter),
				config_line(iter));
			return RET_ERROR;
		}
		if (n->from == NULL && n->method == NULL) {
			fprintf(stderr,
"%s:%u to %u: Update pattern must either contain a Methods: field or reference another one with a From: field.\n",
				config_filename(iter), config_firstline(iter),
				config_line(iter));
			return RET_ERROR;
		}
		if (n->from != NULL && n->fallback != NULL) {
			fprintf(stderr,
"%s:%u to %u: Update pattern may not contain From: and Fallback: fields ad the same time.\n",
				config_filename(iter), config_firstline(iter),
				config_line(iter));
			return RET_ERROR;
		}
		if (n->from != NULL && n->config_set) {
			fprintf(stderr,
"%s:%u to %u: Update pattern may not contain From: and Config: fields ad the same time.\n",
				config_filename(iter), config_firstline(iter),
				config_line(iter));
			return RET_ERROR;
		}
		if (n->suite_from != NULL && strcmp(n->suite_from, "*") != 0 &&
				strncmp(n->suite_from, "*/", 2) != 0 &&
				strchr(n->suite_from, '*') != NULL) {
			fprintf(stderr,
"%s:%u to %u: Unsupported suite pattern '%s'\n",
				config_filename(iter), config_firstline(iter),
				config_line(iter), n->suite_from);
			return RET_ERROR;
		}
		if (n->listhook != NULL && n->shellhook != NULL) {
			fprintf(stderr,
"%s:%u to %u: Only one of ListHook and ListShellHook allowed per update rule\n",
				config_filename(iter), config_firstline(iter),
				config_line(iter));
			return RET_ERROR;
		}
	}
	return linkedlistfinish(privdata_update_pattern,
			thisdata_update_pattern,
			lastdata_p_update_pattern, complete, iter);
}


retvalue updates_getpatterns(struct update_pattern **patterns) {
	struct update_pattern *update = NULL, *u, *v;
	bool progress;
	int i;
	retvalue r;

	r = configfile_parse("updates", IGNORABLE(unknownfield),
			configparser_update_pattern_init,
			finishparseupdate_pattern,
			"update rule",
			updateconfigfields, ARRAYCOUNT(updateconfigfields),
			&update);
	if (RET_IS_OK(r)) {
		for (u = update ; u != NULL ; u = u->next) {
			v = update;
			while (v != NULL &&
			       (v == u || strcmp(v->name, u->name) != 0))
				v = v->next;
			if (v != NULL) {
				// TODO: store line information...
				fprintf(stderr,
"%s/updates: Multiple update patterns named '%s'!\n",
					global.confdir, u->name);
				updates_freepatterns(update);
				return RET_ERROR;
			}
			if (u->from == NULL)
				continue;
			v = update;
			while (v != NULL && strcmp(v->name, u->from) != 0)
				v = v->next;
			if (v == NULL) {
				fprintf(stderr,
"%s/updates: Update pattern '%s' references unknown pattern '%s' via From!\n",
					global.confdir, u->name, u->from);
				updates_freepatterns(update);
				return RET_ERROR;
			}
			u->pattern_from = v;
		}
		/* check for circular references */
		do {
			progress = false;
			for (u = update ; u != NULL ; u = u->next) {
				if (u->visited)
					continue;
				if (u->pattern_from == NULL ||
						u->pattern_from->visited) {
					u->visited = true;
					progress = true;
				}
			}
		} while (progress);
		u = update;
		while (u != NULL && u->visited)
			u = u->next;
		if (u != NULL) {
			/* The actual error is more likely found later.
			 * If someone creates a cycle and a chain into that
			 * more than 1000 rules long, having a slightly
			 * misleading error message will be the last of
			 * their problems... */
			for (i = 0 ; i < 1000 ; i++) {
				u = u->pattern_from;
				assert (u != NULL && !u->visited);
			}
			fprintf(stderr,
"Error: Update rule '%s' part of circular From-referencing.\n",
					u->name);
			updates_freepatterns(update);
			return RET_ERROR;
		}
		*patterns = update;
	} else if (r == RET_NOTHING) {
		assert (update == NULL);
		*patterns = NULL;
		r = RET_OK;
	} else {
		if (r == RET_ERROR_UNKNOWNFIELD)
			(void)fputs(
"To ignore unknown fields use --ignore=unknownfield\n",
					stderr);
		updates_freepatterns(update);
	}
	return r;
}

static inline void markfound(int count, struct update_pattern * const *patterns, const struct update_pattern *lookfor, const struct strlist *searched, const struct strlist *have, bool *found, bool (*hasattribute)(const struct update_pattern*)) {
	int i, j, o;

	for (i = 0 ; i < count ; i++) {
		const struct update_pattern *p = patterns[i];

		/* check if this uses this attribute */
		while (p != NULL && !hasattribute(p))
			p = p->pattern_from;
		if (p != lookfor)
			continue;

		for (j = 0 ; j < have->count ; j++) {
			o = strlist_ofs(searched, have->values[j]);
			if (o >= 0)
				found[o] = true;
		}
		break;
	}
}

static inline bool hasarchitectures(const struct update_pattern *p) {
	return p->architectures_set;
}
static inline bool hascomponents(const struct update_pattern *p) {
	return p->components_set;
}
static inline bool hasudebcomponents(const struct update_pattern *p) {
	return p->udebcomponents_set;
}

/****************************************************************************
 * Step 2: create rules for some distribution based on those patterns       *
 ****************************************************************************/

static retvalue new_deleterule(struct update_origin **origins) {

	struct update_origin *update;

	update = zNEW(struct update_origin);
	if (FAILEDTOALLOC(update))
		return RET_ERROR_OOM;

	*origins = update;
	return RET_OK;
}

static inline char *translate_suite_pattern(const struct update_pattern *p, const char *codename) {
	/* look for first specified suite: */
	while (p != NULL && p->suite_from == NULL)
		p = p->pattern_from;

	if (p == NULL || strcmp(p->suite_from, "*") == 0)
		return strdup(codename);
	if (p->suite_from[0] == '*' && p->suite_from[1] == '/')
		return calc_dirconcat(codename, p->suite_from + 2);
	else if (strchr(p->suite_from, '*') == NULL)
		return strdup(p->suite_from);
	//TODO: implement this
	// but already checked in parsing...
	assert(0);
	return NULL;
}

static retvalue instance_pattern(struct update_pattern *pattern, const struct distribution *distribution, struct update_origin **origins) {

	struct update_origin *update;
	/*@dependant@*/struct update_pattern *declaration, *p, *listscomponents;
	bool ignorehashes[cs_hashCOUNT], ignorerelease, getinrelease;
	const char *verifyrelease;
	retvalue r;

	update = zNEW(struct update_origin);
	if (FAILEDTOALLOC(update))
		return RET_ERROR_OOM;

	update->suite_from = translate_suite_pattern(pattern,
			distribution->codename);
	if (FAILEDTOALLOC(update->suite_from)) {
		free(update);
		return RET_ERROR_OOM;
	}
	if (!pattern->used) {
		declaration = pattern;
		while (declaration->pattern_from != NULL)
			declaration = declaration->pattern_from;
		if (declaration->repository == NULL)
			declaration->repository = remote_repository_prepare(
					declaration->name, declaration->method,
					declaration->fallback,
					&declaration->config);
		if (FAILEDTOALLOC(declaration->repository)) {
			free(update->suite_from);
			free(update);
			return RET_ERROR_OOM;
		}
		pattern->used = true;
	} else {
		declaration = pattern;
		while (declaration->pattern_from != NULL)
			declaration = declaration->pattern_from;
		assert (declaration->repository != NULL);
	}

	update->distribution = distribution;
	update->pattern = pattern;
	update->failed = false;

	p = pattern;
	while (p != NULL && !p->ignorerelease_set)
		p = p->pattern_from;
	if (p == NULL)
		ignorerelease = false;
	else
		ignorerelease = p->ignorerelease;
	p = pattern;
	while (p != NULL && !p->getinrelease_set)
		p = p->pattern_from;
	if (p == NULL)
		getinrelease = true;
	else
		getinrelease = p->getinrelease;
	/* find the first set values: */
	p = pattern;
	while (p != NULL && p->verifyrelease == NULL)
		p = p->pattern_from;
	if (p == NULL)
		verifyrelease = NULL;
	else
		verifyrelease = p->verifyrelease;
	if (!ignorerelease && verifyrelease == NULL && verbose >= 0) {
		fprintf(stderr,
"Warning: No VerifyRelease line in '%s' or any rule it includes via 'From:'.\n"
"Release.gpg cannot be checked unless you tell which key to check with.\n"
"(To avoid this warning and not check signatures add 'VerifyRelease: blindtrust').\n",
				pattern->name);

	}
	p = pattern;
	while (p != NULL && !p->ignorehashes_set)
		p = p->pattern_from;
	if (p == NULL)
		memset(ignorehashes, 0, sizeof(ignorehashes));
	else {
		assert (sizeof(ignorehashes) == sizeof(p->ignorehashes));
		memcpy(ignorehashes, p->ignorehashes, sizeof(ignorehashes));
	}

	listscomponents = NULL;
	p = pattern;
	while (p != NULL && !atom_defined(p->flat)) {
		if (p->components_set || p->udebcomponents_set)
			listscomponents = p;
		p = p->pattern_from;
	}
	update->flat = p != NULL;
	if (update->flat && listscomponents != NULL) {
		fprintf(stderr,
"WARNING: update pattern '%s' (first encountered via '%s' in '%s')\n"
"sets components that are always ignored as '%s' sets Flat mode.\n",
				listscomponents->name, pattern->name,
				distribution->codename, p->name);
	}
	if (p != NULL && !atomlist_in(&distribution->components, p->flat)) {
		fprintf(stderr,
"Error: distribution '%s' uses flat update pattern '%s'\n"
"with target component '%s' which it does not contain!\n",
				distribution->codename,
				pattern->name, atoms_components[p->flat]);
		updates_freeorigins(update);
		return RET_ERROR;
	}
	r = remote_distribution_prepare(declaration->repository,
			update->suite_from, ignorerelease,
			getinrelease, verifyrelease, update->flat,
			ignorehashes, &update->from);
	if (RET_WAS_ERROR(r)) {
		updates_freeorigins(update);
		return r;
	}

	*origins = update;
	return RET_OK;
}

static retvalue findpatterns(struct update_pattern *patterns, const struct distribution *distribution, struct update_pattern ***patterns_p) {
	int i;
	struct update_pattern **used_patterns;

	if (distribution->updates.count == 0)
		return RET_NOTHING;

	used_patterns = nzNEW(distribution->updates.count,
				struct update_pattern *);
	if (FAILEDTOALLOC(used_patterns))
		return RET_ERROR_OOM;

	for (i = 0; i < distribution->updates.count ; i++) {
		const char *name = distribution->updates.values[i];
		struct update_pattern *pattern;

		if (strcmp(name, "-") == 0)
			continue;

		pattern = patterns;
		while (pattern != NULL && strcmp(name, pattern->name) != 0)
			pattern = pattern->next;
		if (pattern == NULL) {
			fprintf(stderr,
"Cannot find definition of upgrade-rule '%s' for distribution '%s'!\n",
						name, distribution->codename);
			if (distribution->selected) {
				free(used_patterns);
				return RET_ERROR;
			} else
				fprintf(stderr,
"This is no error now as '%s' is not used, bug might cause spurious warnings...\n",
						distribution->codename);
		}
		used_patterns[i] = pattern;
	}
	*patterns_p = used_patterns;
	return RET_OK;
}

static retvalue getorigins(struct update_distribution *d) {
	const struct distribution *distribution = d->distribution;
	struct update_origin *updates = NULL;
	retvalue result;
	int i;

	assert (d->patterns != NULL);

	result = RET_NOTHING;
	for (i = 0; i < distribution->updates.count ; i++) {
		struct update_pattern *pattern = d->patterns[i];
		struct update_origin *update SETBUTNOTUSED(= NULL);
		retvalue r;

		if (pattern == NULL) {
			assert (strcmp(distribution->updates.values[i], "-") == 0);
			r = new_deleterule(&update);
		} else {
			r = instance_pattern(pattern, distribution, &update);
		}
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		if (RET_IS_OK(r)) {
			assert (update != NULL);
			update->next = updates;
			updates = update;
		}
	}

	if (RET_WAS_ERROR(result)) {
		updates_freeorigins(updates);
	} else {
		d->origins = updates;
	}
	return result;
}

/****************************************************************************
 * Step 3: calculate which remote indices are to be retrieved and processed *
 ****************************************************************************/

static inline bool addremoteindex(struct update_origin *origin, struct target *target, struct update_target *updatetargets, const char *architecture, const char *component) {
	struct update_index_connector *uindex;
	const struct update_pattern *p;

	uindex = zNEW(struct update_index_connector);
	if (FAILEDTOALLOC(uindex))
		return false;

	p = origin->pattern;
	while (p != NULL && !p->downloadlistsas_set)
		p = p->pattern_from;

	uindex->origin = origin;
	uindex->remote = remote_index(origin->from,
			architecture, component,
			target->packagetype,
			(p == NULL)?NULL:&p->downloadlistsas);
	if (FAILEDTOALLOC(uindex->remote)) {
		free(uindex);
		return false;
	}
	assert (!origin->flat);
	uindex->next = updatetargets->indices;
	uindex->ignorewrongarchitecture = strcmp(architecture,
				atoms_architectures[
				target->architecture]) != 0;
	updatetargets->indices = uindex;
	return true;
}

static retvalue addorigintotarget(struct update_origin *origin, struct target *target, struct distribution *distribution, struct update_target *updatetargets) {
	const struct update_pattern *p;
	const struct strlist *c_from = NULL, *c_into = NULL;
	const struct strlist *a_from = NULL, *a_into = NULL;
	const char *architecture = atoms_architectures[target->architecture];
	const char *component = atoms_components[target->component];
	int ai, ci;

	assert (origin != NULL && origin->pattern != NULL);

	p = origin->pattern;
	while (p != NULL && !p->architectures_set)
		p = p->pattern_from;
	if (p != NULL) {
		a_from = &p->architectures_from;
		a_into = &p->architectures_into;
	}
	p = origin->pattern;
	if (target->packagetype == pt_udeb)  {
		while (p != NULL && !p->udebcomponents_set)
			p = p->pattern_from;
		if (p != NULL) {
			c_from = &p->udebcomponents_from;
			c_into = &p->udebcomponents_into;
		}
	} else {
		while (p != NULL && !p->components_set)
			p = p->pattern_from;
		if (p != NULL) {
			c_from = &p->components_from;
			c_into = &p->components_into;
		}
	}

	if (a_into == NULL) {
		assert (atomlist_in(&distribution->architectures,
					target->architecture));

		if (c_into == NULL) {
			if (!addremoteindex(origin, target, updatetargets,
						architecture, component))
				return RET_ERROR_OOM;
			return RET_OK;
		}
		for (ci = 0 ; ci < c_into->count ; ci++) {
			if (strcmp(c_into->values[ci], component) != 0)
				continue;

			if (!addremoteindex(origin, target, updatetargets,
					architecture, c_from->values[ci]))
				return RET_ERROR_OOM;
		}
		return RET_OK;
	}
	for (ai = 0 ; ai < a_into->count ; ai++) {
		if (strcmp(architecture, a_into->values[ai]) != 0)
			continue;
		if (c_into == NULL) {
			if (!addremoteindex(origin, target, updatetargets,
					a_from->values[ai], component))
				return RET_ERROR_OOM;
			continue;
		}

		for (ci = 0 ; ci < c_into->count ; ci++) {
			if (strcmp(component, c_into->values[ci]) != 0)
				continue;

			if (!addremoteindex(origin, target, updatetargets,
					a_from->values[ai], c_from->values[ci]))
				return RET_ERROR_OOM;
		}
	}
	return RET_OK;
}

static retvalue addflatorigintotarget(struct update_origin *origin, struct target *target, struct update_target *updatetargets) {
	const struct update_pattern *p;
	const struct strlist *a_into;
	const struct encoding_preferences *downloadlistsas;
	int ai;

	assert (origin != NULL);

	if (target->packagetype == pt_udeb)
		return RET_NOTHING;

	p = origin->pattern;
	while (p != NULL && !p->downloadlistsas_set)
		p = p->pattern_from;
	if (p == NULL)
		downloadlistsas = NULL;
	else
		downloadlistsas = &p->downloadlistsas;

	p = origin->pattern;
	while (p != NULL && !atom_defined(p->flat))
		p = p->pattern_from;
	assert (p != NULL);
	if (p->flat != target->component)
		return RET_NOTHING;

	p = origin->pattern;
	while (p != NULL && !p->architectures_set)
		p = p->pattern_from;
	if (p == NULL) {
		struct update_index_connector *uindex;

		uindex = zNEW(struct update_index_connector);
		if (FAILEDTOALLOC(uindex))
			return RET_ERROR_OOM;


		uindex->origin = origin;
		uindex->remote = remote_flat_index(origin->from,
				target->packagetype,
				downloadlistsas);
		if (FAILEDTOALLOC(uindex->remote)) {
			free(uindex);
			return RET_ERROR_OOM;
		}
		uindex->next = updatetargets->indices;
		assert (origin->flat);
		uindex->ignorewrongarchitecture = true;
		updatetargets->indices = uindex;
		return RET_OK;
	}

	a_into = &p->architectures_into;

	for (ai = 0 ; ai < a_into->count ; ai++) {
		struct update_index_connector *uindex;
		const char *a = atoms_architectures[target->architecture];

		if (strcmp(a_into->values[ai], a) != 0)
			continue;

		uindex = zNEW(struct update_index_connector);
		if (FAILEDTOALLOC(uindex))
			return RET_ERROR_OOM;

		uindex->origin = origin;
		uindex->remote = remote_flat_index(origin->from,
				target->packagetype,
				downloadlistsas);
		if (FAILEDTOALLOC(uindex->remote)) {
			free(uindex);
			return RET_ERROR_OOM;
		}
		uindex->next = updatetargets->indices;
		assert (origin->flat);
		uindex->ignorewrongarchitecture = true;
		updatetargets->indices = uindex;
	}
	return RET_OK;
}

static retvalue adddeleteruletotarget(struct update_target *updatetargets) {
	struct update_index_connector *uindex;

	uindex = zNEW(struct update_index_connector);
	if (FAILEDTOALLOC(uindex))
		return RET_ERROR_OOM;
	uindex->next = updatetargets->indices;
	updatetargets->indices = uindex;
	return RET_OK;
}

static retvalue gettargets(struct update_origin *origins, struct distribution *distribution, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *types,  struct update_target **ts) {
	struct target *target;
	struct update_origin *origin;
	struct update_target *updatetargets;
	retvalue r;

	updatetargets = NULL;

	for (target = distribution->targets ; target != NULL ;
	                                      target = target->next) {
		if (!target_matches(target, components, architectures, types))
			continue;
		r = newupdatetarget(&updatetargets, target);
		if (RET_WAS_ERROR(r)) {
			updates_freetargets(updatetargets);
			return r;
		}

		for (origin = origins ; origin != NULL ; origin=origin->next) {
			if (origin->pattern == NULL)
				r = adddeleteruletotarget(updatetargets);
			else if (!origin->flat)
				r = addorigintotarget(origin, target,
						distribution, updatetargets);
			else
				r = addflatorigintotarget(origin, target,
						updatetargets);
			if (RET_WAS_ERROR(r)) {
				updates_freetargets(updatetargets);
				return r;
			}
		}
	}

	*ts = updatetargets;
	return RET_OK;
}

static inline retvalue findmissingupdate(const struct distribution *distribution, struct update_origin *updates) {
	retvalue result;
	struct update_origin *last;
	int count;

	assert (updates != NULL);
	last = updates;
	count = 1;
	while (last->next != NULL) {
		last = last->next;
		count++;
	}

	result = RET_OK;

	if (count != distribution->updates.count) {
		int i;

		// TODO: why is this here? can this actually happen?

		for (i=0; i<distribution->updates.count; i++){
			const char *update = distribution->updates.values[i];
			struct update_origin *u;

			u = updates;
			while (u != NULL && strcmp(u->pattern->name, update) != 0)
				u = u->next;
			if (u == NULL) {
				fprintf(stderr,
"Update '%s' is listed in distribution '%s', but was not found!\n",
					update, distribution->codename);
				result = RET_ERROR_MISSING;
				break;
			}
		}
		if (RET_IS_OK(result)) {
			fprintf(stderr,
"Did you write an update two times in the update-line of '%s'?\n",
					distribution->codename);
			result = RET_NOTHING;
		}
	}

	return result;
}

retvalue updates_calcindices(struct update_pattern *patterns, struct distribution *distributions, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *types, struct update_distribution **update_distributions) {
	struct distribution *distribution;
	struct update_distribution *u_ds;
	retvalue result, r;

	u_ds = NULL;
	result = RET_NOTHING;

	for (distribution = distributions ; distribution != NULL ;
			       distribution = distribution->next) {
		struct update_distribution *u_d;
		struct update_pattern **translated_updates;

		if (!distribution->selected)
			continue;

		r = findpatterns(patterns, distribution, &translated_updates);
		if (r == RET_NOTHING)
			continue;
		if (RET_WAS_ERROR(r)) {
			result = r;
			break;
		}

		u_d = zNEW(struct update_distribution);
		if (FAILEDTOALLOC(u_d)) {
			free(translated_updates);
			result = RET_ERROR_OOM;
			break;
		}

		u_d->distribution = distribution;
		u_d->patterns = translated_updates;
		u_d->next = u_ds;
		u_ds = u_d;

		r = getorigins(u_d);
		if (RET_WAS_ERROR(r)) {
			result = r;
			break;
		}
		if (RET_IS_OK(r)) {
			/* Check if we got all: */
			r = findmissingupdate(distribution, u_d->origins);
			if (RET_WAS_ERROR(r)) {
				result = r;
				break;
			}

			r = gettargets(u_d->origins, distribution,
					components, architectures, types,
					&u_d->targets);
			if (RET_WAS_ERROR(r)) {
				result = r;
				break;
			}
		}
		result = RET_OK;
	}
	if (RET_IS_OK(result)) {
		*update_distributions = u_ds;
	} else
		updates_freeupdatedistributions(u_ds);
	return result;
}

/****************************************************************************
 * Step 5: preperations for actually doing anything:                        *
 * 		- printing some warnings                                    *
 * 		- prepare distribution for writing                          *
 * 		- rest moved to remote_startup                              *
 ****************************************************************************/

static retvalue updates_startup(struct aptmethodrun *run, struct update_distribution *distributions, bool willwrite) {
	retvalue r;
	struct update_distribution *d;

	for (d=distributions ; d != NULL ; d=d->next) {
		if (willwrite) {
			r = distribution_prepareforwriting(d->distribution);
			if (RET_WAS_ERROR(r))
				return r;
		}
		r = distribution_loadalloverrides(d->distribution);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return remote_startup(run);
}

/****************************************************************************
 * Step 6: queue downloading of list of lists (Release, Release.gpg, ...)   *
 ****************************************************************************
 -> moved to remoterepository.c */

/****************************************************************************
 * Step 7: queue downloading of lists                                       *
 *         (using information from previously downloaded meta-lists)        *
 ****************************************************************************
 -> moved to remoterepository.c */

/****************************************************************************
 * Step 8: call possible list hooks allowing them to modify the lists       *
 ****************************************************************************/

static retvalue calllisthook(struct update_target *ut, struct update_index_connector *f, const char *listhook) {
	struct update_origin *origin = f->origin;
	const char *oldfilename = remote_index_file(f->remote);
	const char *oldbasefilename = remote_index_basefile(f->remote);
	char *newfilename;
	pid_t child, c;
	int status;

	/* distribution, component, architecture and pattern specific... */
	newfilename = genlistsfilename(oldbasefilename, 5, "",
			ut->target->distribution->codename,
			atoms_components[ut->target->component],
			atoms_architectures[ut->target->architecture],
			origin->pattern->name, ENDOFARGUMENTS);
	if (FAILEDTOALLOC(newfilename))
		return RET_ERROR_OOM;
	child = fork();
	if (child < 0) {
		int e = errno;
		free(newfilename);
		fprintf(stderr, "Error %d while forking for listhook: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}
	if (child == 0) {
		int e;
		(void)closefrom(3);
		sethookenvironment(NULL, NULL, NULL, NULL);
		setenv("REPREPRO_FILTER_CODENAME",
				ut->target->distribution->codename, true);
		setenv("REPREPRO_FILTER_PACKAGETYPE",
				atoms_architectures[ut->target->packagetype],
				true);
		setenv("REPREPRO_FILTER_COMPONENT",
				atoms_components[ut->target->component],
				true);
		setenv("REPREPRO_FILTER_ARCHITECTURE",
				atoms_architectures[ut->target->architecture],
				true);
		setenv("REPREPRO_FILTER_PATTERN", origin->pattern->name, true);
		execl(listhook, listhook, oldfilename, newfilename,
				ENDOFARGUMENTS);
		e = errno;
		fprintf(stderr, "Error %d while executing '%s': %s\n",
				e, listhook, strerror(e));
		exit(255);
	}
	if (verbose > 5)
		fprintf(stderr, "Called %s '%s' '%s'\n", listhook,
				oldfilename, newfilename);
	f->afterhookfilename = newfilename;
	do {
		c = waitpid(child, &status, WUNTRACED);
		if (c < 0) {
			int e = errno;
			fprintf(stderr,
"Error %d while waiting for hook '%s' to finish: %s\n",
					e, listhook, strerror(e));
			return RET_ERRNO(e);
		}
	} while (c != child);
	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) == 0) {
			if (verbose > 5)
				fprintf(stderr,
"Listhook successfully returned!\n");
			return RET_OK;
		} else {
			fprintf(stderr,
"Listhook failed with exitcode %d!\n",
				(int)WEXITSTATUS(status));
			return RET_ERROR;
		}
	} else {
		fprintf(stderr,
"Listhook terminated abnormally. (status is %x)!\n",
			status);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue callshellhook(struct update_target *ut, struct update_index_connector *f, const char *shellhook) {
	struct update_origin *origin = f->origin;
	const char *oldfilename = remote_index_file(f->remote);
	const char *oldbasefilename = remote_index_basefile(f->remote);
	char *newfilename;
	pid_t child, c;
	int status;
	int infd, outfd;

	/* distribution, component, architecture and pattern specific... */
	newfilename = genlistsfilename(oldbasefilename, 5, "",
			ut->target->distribution->codename,
			atoms_components[ut->target->component],
			atoms_architectures[ut->target->architecture],
			origin->pattern->name, ENDOFARGUMENTS);
	if (FAILEDTOALLOC(newfilename))
		return RET_ERROR_OOM;
	infd = open(oldfilename, O_RDONLY|O_NOCTTY|O_NOFOLLOW);
	if (infd < 0) {
		int e = errno;

		fprintf(stderr,
"Error %d opening expected file '%s': %s!\n"
"Something strange must go on!\n", e, oldfilename, strerror(e));
		return RET_ERRNO(e);
	}
	(void)unlink(newfilename);
	outfd = open(newfilename,
			O_WRONLY|O_NOCTTY|O_NOFOLLOW|O_CREAT|O_EXCL, 0666);
	if (outfd < 0) {
		int e = errno;

		fprintf(stderr, "Error %d creating '%s': %s!\n", e,
				newfilename, strerror(e));
		close(infd);
		return RET_ERRNO(e);
	}
	child = fork();
	if (child < 0) {
		int e = errno;
		free(newfilename);
		fprintf(stderr, "Error %d while forking for shell hook: %s\n",
				e, strerror(e));
		(void)close(infd);
		(void)close(outfd);
		(void)unlink(newfilename);
		return RET_ERRNO(e);
	}
	if (child == 0) {
		int e;

		assert (dup2(infd, 0) == 0);
		assert (dup2(outfd, 1) == 1);
		close(infd);
		close(outfd);
		(void)closefrom(3);
		sethookenvironment(NULL, NULL, NULL, NULL);
		setenv("REPREPRO_FILTER_CODENAME",
				ut->target->distribution->codename, true);
		setenv("REPREPRO_FILTER_PACKAGETYPE",
				atoms_architectures[ut->target->packagetype],
				true);
		setenv("REPREPRO_FILTER_COMPONENT",
				atoms_components[ut->target->component],
				true);
		setenv("REPREPRO_FILTER_ARCHITECTURE",
				atoms_architectures[ut->target->architecture],
				true);
		setenv("REPREPRO_FILTER_PATTERN", origin->pattern->name, true);
		execlp("sh", "sh", "-c", shellhook, ENDOFARGUMENTS);
		e = errno;
		fprintf(stderr, "Error %d while executing sh -c '%s': %s\n",
				e, shellhook, strerror(e));
		exit(255);
	}
	(void)close(infd);
	(void)close(outfd);
	if (verbose > 5)
		fprintf(stderr, "Called sh -c '%s' <'%s' >'%s'\n", shellhook,
				oldfilename, newfilename);
	f->afterhookfilename = newfilename;
	do {
		c = waitpid(child, &status, WUNTRACED);
		if (c < 0) {
			int e = errno;
			fprintf(stderr,
"Error %d while waiting for shell hook '%s' to finish: %s\n",
					e, shellhook, strerror(e));
			return RET_ERRNO(e);
		}
	} while (c != child);
	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) == 0) {
			if (verbose > 5)
				fprintf(stderr,
"shell hook successfully returned!\n");
			return RET_OK;
		} else {
			fprintf(stderr,
"shell hook '%s' failed with exitcode %d!\n",
					shellhook, (int)WEXITSTATUS(status));
			return RET_ERROR;
		}
	} else {
		fprintf(stderr,
"shell hook '%s' terminated abnormally. (status is %x)!\n",
				shellhook, status);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue calllisthooks(struct update_distribution *d) {
	retvalue result, r;
	struct update_target *target;
	struct update_index_connector *uindex;

	result = RET_NOTHING;
	for (target = d->targets; target != NULL ; target = target->next) {
		if (target->nothingnew)
			continue;
		/* if anything is new, we will to need to look at
		 * all (in case there are delete rules) */
		for (uindex = target->indices ; uindex != NULL ;
				uindex = uindex->next) {
			const struct update_pattern *p;

			if (uindex->remote == NULL)
				continue;
			if (uindex->failed)
				continue;
			p = uindex->origin->pattern;
			while (p != NULL && p->listhook == NULL
			                 && p->shellhook == NULL)
				p = p->pattern_from;
			if (p == NULL)
				continue;
			if (p->listhook != NULL)
				r = calllisthook(target, uindex, p->listhook);
			else {
				assert (p->shellhook != NULL);
				r = callshellhook(target, uindex, p->shellhook);
			}
			if (RET_WAS_ERROR(r)) {
				uindex->failed = true;
				return r;
			}
			RET_UPDATE(result, r);
		}
	}
	return result;
}

static retvalue updates_calllisthooks(struct update_distribution *distributions) {
	retvalue result, r;
	struct update_distribution *d;

	result = RET_NOTHING;
	for (d=distributions ; d != NULL ; d=d->next) {
		r = calllisthooks(d);
		RET_UPDATE(result, r);
	}
	return result;
}

/****************************************************************************
 * Step 9: search for missing packages i.e. needing to be added or upgraded *
 *         (all the logic in upgradelist.c, this is only clue code)         *
 ****************************************************************************/

static upgrade_decision ud_decide_by_pattern(void *privdata, struct target *target, struct package *new, /*@null@*/const char *old_version) {
	const struct update_pattern *pattern = privdata, *p;
	retvalue r;
	upgrade_decision decision = UD_UPGRADE;
	enum filterlisttype listdecision;
	bool cmdline_still_undecided;

	if (target->packagetype == pt_dsc) {
		p = pattern;
		while (p != NULL && !p->filtersrclist.set)
			p = p->pattern_from;
		if (p != NULL)
			listdecision = filterlist_find(new->name, new->version,
					&p->filtersrclist);
		else {
			p = pattern;
			while (p != NULL && !p->filterlist.set)
				p = p->pattern_from;
			if (p == NULL)
				listdecision = flt_install;
			else
				listdecision = filterlist_find(new->name,
						new->version, &p->filterlist);
		}
	} else {
		p = pattern;
		while (p != NULL && !p->filterlist.set)
			p = p->pattern_from;
		if (p != NULL)
			listdecision = filterlist_find(new->name, new->version,
					&p->filterlist);
		else {
			p = pattern;
			while (p != NULL && !p->filtersrclist.set)
				p = p->pattern_from;
			if (p == NULL)
				listdecision = flt_install;
			else
				listdecision = filterlist_find(new->source,
						new->sourceversion,
						&p->filtersrclist);
		}
	}

	switch (listdecision) {
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
			assert (listdecision != listdecision);
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
	}

	p = pattern;
	while (p != NULL && !p->includecondition_set)
		p = p->pattern_from;
	if (p != NULL) {
		r = term_decidepackage(p->includecondition, new, target);
		if (RET_WAS_ERROR(r))
			return UD_ERROR;
		if (r == RET_NOTHING) {
			return UD_NO;
		}
	}

	if (target->packagetype != pt_dsc)
		return decision;

	p = pattern;
	while (p != NULL && !p->omitextrasource_set)
		p = p->pattern_from;
	/* if unset or set to true, ignore new->source having that field */
	if (p == NULL || p->omitextrasource == true) {
		if (chunk_gettruth(new->control, "Extra-Source-Only"))
			return UD_NO;
	}

	return decision;
}


static inline retvalue searchformissing(/*@null@*/FILE *out, struct update_target *u) {
	struct update_index_connector *uindex;
	retvalue result, r;

	if (u->nothingnew) {
		if (u->indices == NULL && verbose >= 4 && out != NULL)
			fprintf(out,
"  nothing to do for '%s'\n",
				u->target->identifier);
		else if (u->indices != NULL && verbose >= 0 && out != NULL)
			fprintf(out,
"  nothing new for '%s' (use --noskipold to process anyway)\n",
				u->target->identifier);
		return RET_NOTHING;
	}
	if (verbose > 2 && out != NULL)
		fprintf(out, "  processing updates for '%s'\n",
				u->target->identifier);
	r = upgradelist_initialize(&u->upgradelist, u->target);
	if (RET_WAS_ERROR(r))
		return r;

	result = RET_NOTHING;

	for (uindex = u->indices ; uindex != NULL ; uindex = uindex->next) {
		const char *filename;

		if (uindex->origin == NULL) {
			if (verbose > 4 && out != NULL)
				fprintf(out,
"  marking everything to be deleted\n");
			r = upgradelist_deleteall(u->upgradelist);
			if (RET_WAS_ERROR(r))
				u->incomplete = true;
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				return result;
			u->ignoredelete = false;
			continue;
		}

		if (uindex->afterhookfilename != NULL)
			filename = uindex->afterhookfilename;
		else
			filename = remote_index_file(uindex->remote);

		if (uindex->failed || uindex->origin->failed) {
			if (verbose >= 1)
				fprintf(stderr,
"  missing '%s'\n", filename);
			u->incomplete = true;
			u->ignoredelete = true;
			continue;
		}

		if (verbose > 4 && out != NULL)
			fprintf(out, "  reading '%s'\n", filename);
		r = upgradelist_update(u->upgradelist, uindex,
				filename,
				ud_decide_by_pattern,
				(void*)uindex->origin->pattern,
				uindex->ignorewrongarchitecture);
		if (RET_WAS_ERROR(r)) {
			u->incomplete = true;
			u->ignoredelete = true;
		}
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			return result;
	}

	return result;
}

static retvalue updates_readindices(/*@null@*/FILE *out, struct update_distribution *d) {
	retvalue result, r;
	struct update_target *u;

	result = RET_NOTHING;
	for (u=d->targets ; u != NULL ; u=u->next) {
		r = searchformissing(out, u);
		if (RET_WAS_ERROR(r))
			u->incomplete = true;
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}
	return result;
}

/****************************************************************************
 * Step 10: enqueue downloading of missing packages                         *
 ****************************************************************************/

static retvalue enqueue_upgrade_package(void *calldata, const struct checksumsarray *origfiles, const struct strlist *filekeys, void *privdata) {
	struct update_index_connector *uindex = privdata;
	struct aptmethod *aptmethod;
	struct downloadcache *cache = calldata;

	assert(privdata != NULL);
	aptmethod = remote_aptmethod(uindex->origin->from);
	assert(aptmethod != NULL);
	return downloadcache_addfiles(cache, aptmethod, origfiles, filekeys);
}

static retvalue updates_enqueue(struct downloadcache *cache, struct update_distribution *distribution) {
	retvalue result, r;
	struct update_target *u;

	result = RET_NOTHING;
	for (u=distribution->targets ; u != NULL ; u=u->next) {
		if (u->nothingnew)
			continue;
		r = upgradelist_enqueue(u->upgradelist, enqueue_upgrade_package,
				cache);
		if (RET_WAS_ERROR(r))
			u->incomplete = true;
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}
	return result;
}

/****************************************************************************
 * Step 11: install the missing packages                                    *
 *          (missing files should have been downloaded first)               *
 ****************************************************************************/
static bool isbigdelete(struct update_distribution *d) {
	struct update_target *u, *v;

	for (u = d->targets ; u != NULL ; u=u->next) {
		if (u->nothingnew || u->ignoredelete)
			continue;
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

static void updates_from_callback(void *privdata, const char **rule_p, const char **from_p) {
	struct update_index_connector *uindex = privdata;

	*from_p = uindex->origin->suite_from;
	*rule_p = uindex->origin->pattern->name;
}

static retvalue updates_install(struct update_distribution *distribution) {
	retvalue result, r;
	struct update_target *u;
	struct distribution *d = distribution->distribution;

	assert (logger_isprepared(d->logger));

	result = RET_NOTHING;
	for (u=distribution->targets ; u != NULL ; u=u->next) {
		if (u->nothingnew)
			continue;
		r = upgradelist_install(u->upgradelist, d->logger,
				u->ignoredelete, updates_from_callback);
		RET_UPDATE(d->status, r);
		if (RET_WAS_ERROR(r))
			u->incomplete = true;
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

/****************************************************************************
 * Step 12: mark index files as processed, so they won't process a second   *
 *          time, unless --noskipold is given                               *
 ****************************************************************************/

static void markdone(struct update_distribution *d) {
	struct markdonefile *done;
	struct update_index_connector *i;
	struct update_target *t;
	retvalue r;

	r = markdone_create(d->distribution->codename, &done);
	if (!RET_IS_OK(r))
		return;

	for (t = d->targets ; t != NULL ; t = t->next) {
		if (t->incomplete)
			continue;
		markdone_target(done, t->target->identifier);
		for (i = t->indices ; i != NULL ; i = i->next)
			if (i->remote == NULL)
				markdone_cleaner(done);
			else
				remote_index_markdone(i->remote, done);
	}
	markdone_finish(done);
}


/****************************************************************************
 * All together now: everything done step after step, in between telling    *
 * the apt methods to actually download what was enqueued.                  *
 ****************************************************************************/

static retvalue markold(struct update_distribution *ud) {
	struct update_target *ut;
	struct update_index_connector *ui;
	retvalue r;
	struct donefile *donefile;
	const char *identifier;

	r = donefile_open(ud->distribution->codename, &donefile);
	if (!RET_IS_OK(r))
		return r;

	while (donefile_nexttarget(donefile, &identifier)) {
		ut = ud->targets;
		while (ut != NULL && strcmp(identifier,
					ut->target->identifier) != 0)
			ut = ut->next;
		if (ut == NULL)
			continue;
		ut->nothingnew = true;
		for (ui = ut->indices ; ui != NULL ; ui = ui->next) {
			/* if the order does not match, it does not matter
			 * if they are new or not, they should be processed
			 * anyway */

			if (ui->remote == NULL) {
				if (!donefile_iscleaner(donefile)) {
					ut->nothingnew = false;
					break;
				}
				continue;
			}
			if (remote_index_isnew(ui->remote, donefile)) {
				ut->nothingnew = false;
				break;
			}
		}

	}
	donefile_close(donefile);
	return RET_OK;
}

static retvalue updates_preparelists(struct aptmethodrun *run, struct update_distribution *distributions, bool nolistsdownload, bool skipold, bool *anythingtodo) {
	struct update_distribution *d;
	struct update_target *ut;
	struct update_index_connector *ui;
	retvalue r;

	r = remote_preparemetalists(run, nolistsdownload);
	if (RET_WAS_ERROR(r))
		return r;

	for (d = distributions ; d != NULL ; d = d->next) {
		/* first check what is old */
		if (skipold) {
			r = markold(d);
			if (RET_WAS_ERROR(r))
				return r;
		}
		/* we need anything that is needed in a target
		 * where something is new (as new might mean
		 * a package is left hiding leftmore packages,
		 * and everything in rightmore packages is needed
		 * to see what in the new takes effect) */
		for (ut = d->targets; ut != NULL ; ut = ut->next) {
			if (ut->nothingnew)
				continue;
			if (ut->indices == NULL) {
				ut->nothingnew = true;
				continue;
			}
			for (ui = ut->indices ; ui != NULL ; ui = ui->next) {
				if (ui->remote == NULL)
					continue;
				remote_index_needed(ui->remote);
				*anythingtodo = true;
			}
		}
	}

	r = remote_preparelists(run, nolistsdownload);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}

static retvalue updates_prepare(struct update_distribution *distributions, bool willwrite, bool nolistsdownload, bool skipold, struct aptmethodrun **run_p) {
	retvalue result, r;
	struct aptmethodrun *run;
	bool anythingtodo = !skipold;

	r = aptmethod_initialize_run(&run);
	if (RET_WAS_ERROR(r))
		return r;

	/* preperations */
	result = updates_startup(run, distributions, willwrite);
	if (RET_WAS_ERROR(result)) {
		aptmethod_shutdown(run);
		return result;
	}

	r = updates_preparelists(run, distributions, nolistsdownload, skipold,
			&anythingtodo);
	RET_UPDATE(result, r);
	if (RET_WAS_ERROR(result)) {
		aptmethod_shutdown(run);
		return result;
	}
	if (!anythingtodo && skipold) {
		if (verbose >= 0) {
			if (willwrite)
				printf(
"Nothing to do found. (Use --noskipold to force processing)\n");
			else
				fprintf(stderr,
"Nothing to do found. (Use --noskipold to force processing)\n");
		}

		aptmethod_shutdown(run);
		return RET_NOTHING;
	}

	/* Call ListHooks (if given) on the downloaded index files.
	 * (This is done even when nolistsdownload is given, as otherwise
	 *  the filename to look in is not calculated) */
	r = updates_calllisthooks(distributions);
	RET_UPDATE(result, r);
	if (RET_WAS_ERROR(result)) {
		aptmethod_shutdown(run);
		return result;
	}

	*run_p = run;
	return RET_OK;
}


retvalue updates_update(struct update_distribution *distributions, bool nolistsdownload, bool skipold, enum spacecheckmode mode, off_t reserveddb, off_t reservedother) {
	retvalue result, r;
	struct update_distribution *d;
	struct downloadcache *cache;
	struct aptmethodrun *run;
	bool todo;

	causingfile = NULL;

	result = updates_prepare(distributions, true, nolistsdownload, skipold,
			&run);
	if (!RET_IS_OK(result))
		return result;

	/* Then get all packages */
	if (verbose >= 0)
		printf("Calculating packages to get...\n");
	r = downloadcache_initialize(mode, reserveddb, reservedother, &cache);
	if (!RET_IS_OK(r)) {
		aptmethod_shutdown(run);
		RET_UPDATE(result, r);
		return result;
	}

	todo = false;
	for (d=distributions ; d != NULL ; d=d->next) {
		r = updates_readindices(stdout, d);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		if (global.onlysmalldeletes) {
			if (isbigdelete(d))
				continue;
		}
		r = updates_enqueue(cache, d);
		if (RET_IS_OK(r))
			todo = true;
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}
	if (!RET_WAS_ERROR(result)) {
		r = space_check(cache->devices);
		RET_ENDUPDATE(result, r);
	}
	if (!RET_WAS_ERROR(result) && !todo) {
		for (d=distributions ; !todo && d != NULL ; d=d->next) {
			struct update_target *u;
			if (d->distribution->omitted)
				continue;
			for (u = d->targets ; u != NULL ; u = u->next) {
				if (u->nothingnew || u->ignoredelete)
					continue;
				if (upgradelist_woulddelete(u->upgradelist)) {
					todo = true;
					break;
				}
			}
		}
	}

	if (RET_WAS_ERROR(result) || !todo) {
		for (d=distributions ; d != NULL ; d=d->next) {
			struct update_target *u;
			if (d->distribution->omitted) {
				fprintf(stderr,
"Not processing updates for '%s' because of --onlysmalldeletes!\n",
					d->distribution->codename);
			} else if (RET_IS_OK(result))
				markdone(d);
			for (u=d->targets ; u != NULL ; u=u->next) {
				upgradelist_free(u->upgradelist);
				u->upgradelist = NULL;
			}
		}
		r = downloadcache_free(cache);
		RET_UPDATE(result, r);
		aptmethod_shutdown(run);
		return result;
	}
	if (verbose >= 0)
		printf("Getting packages...\n");
	r = aptmethod_download(run);
	RET_UPDATE(result, r);
	r = downloadcache_free(cache);
	RET_ENDUPDATE(result, r);
	if (verbose > 0)
		printf("Shutting down aptmethods...\n");
	r = aptmethod_shutdown(run);
	RET_UPDATE(result, r);

	if (RET_WAS_ERROR(result)) {
		for (d=distributions ; d != NULL ; d=d->next) {
			struct update_target *u;
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
		if (d->distribution->omitted)
			continue;
		r = updates_install(d);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
	}

	for (d=distributions ; d != NULL ; d=d->next) {
		if (d->distribution->omitted) {
			fprintf(stderr,
"Not processing updates for '%s' because of --onlysmalldeletes!\n",
					d->distribution->codename);
		} else
			markdone(d);
	}
	logger_wait();

	return result;
}

/****************************************************************************
 * Alternatively, don't download and install, but list what is needed to be *
 * done. (For the checkupdate command)                                      *
 ****************************************************************************/

static void upgrade_dumppackage(const char *packagename, /*@null@*/const char *oldversion, /*@null@*/const char *newversion, /*@null@*/const char *bestcandidate, /*@null@*/const struct strlist *newfilekeys, /*@null@*/const char *newcontrol, void *privdata) {
	struct update_index_connector *uindex = privdata;

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
		const char *via = uindex->origin->pattern->name;

		assert (newfilekeys != NULL);
		assert (newcontrol != NULL);
		if (oldversion != NULL)
			(void)printf(
"'%s': '%s' will be upgraded to '%s' (from '%s'):\n files needed: ",
					packagename, oldversion,
					newversion, via);
		else
			(void)printf(
"'%s': newly installed as '%s' (from '%s'):\n files needed: ",
					packagename, newversion, via);
		(void)strlist_fprint(stdout, newfilekeys);
		if (verbose > 2)
			(void)printf("\n installing as: '%s'\n",
					newcontrol);
		else
			(void)putchar('\n');
	}
}

static void updates_dump(struct update_distribution *distribution) {
	struct update_target *u;

	for (u=distribution->targets ; u != NULL ; u=u->next) {
		if (u->nothingnew)
			continue;
		printf("Updates needed for '%s':\n", u->target->identifier);
		upgradelist_dump(u->upgradelist, upgrade_dumppackage);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
	}
}

static void upgrade_dumplistpackage(const char *packagename, /*@null@*/const char *oldversion, /*@null@*/const char *newversion, /*@null@*/const char *bestcandidate, /*@null@*/const struct strlist *newfilekeys, /*@null@*/const char *newcontrol, void *privdata) {
	struct update_index_connector *uindex = privdata;

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
		const char *via = uindex->origin->pattern->name;

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

static void updates_dumplist(struct update_distribution *distribution) {
	struct update_target *u;

	for (u=distribution->targets ; u != NULL ; u=u->next) {
		if (u->nothingnew)
			continue;
		printf("Updates needed for '%s':\n", u->target->identifier);
		upgradelist_dump(u->upgradelist, upgrade_dumplistpackage);
		upgradelist_free(u->upgradelist);
		u->upgradelist = NULL;
	}
}

retvalue updates_checkupdate(struct update_distribution *distributions, bool nolistsdownload, bool skipold) {
	struct update_distribution *d;
	retvalue result, r;
	struct aptmethodrun *run;

	result = updates_prepare(distributions, false, nolistsdownload, skipold,
			&run);
	if (!RET_IS_OK(result))
		return result;

	if (verbose > 0)
		fprintf(stderr, "Shutting down aptmethods...\n");
	r = aptmethod_shutdown(run);
	RET_UPDATE(result, r);
	if (RET_WAS_ERROR(result)) {
		return result;
	}

	/* Then look what packages to get */
	if (verbose >= 0)
		fprintf(stderr, "Calculating packages to get...\n");

	for (d=distributions ; d != NULL ; d=d->next) {
		r = updates_readindices(stderr, d);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		updates_dump(d);
	}

	return result;
}

retvalue updates_dumpupdate(struct update_distribution *distributions, bool nolistsdownload, bool skipold) {
	struct update_distribution *d;
	retvalue result, r;
	struct aptmethodrun *run;

	result = updates_prepare(distributions, false, nolistsdownload, skipold,
			&run);
	if (!RET_IS_OK(result))
		return result;

	r = aptmethod_shutdown(run);
	RET_UPDATE(result, r);
	if (RET_WAS_ERROR(result)) {
		return result;
	}

	for (d=distributions ; d != NULL ; d=d->next) {
		r = updates_readindices(NULL, d);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		updates_dumplist(d);
	}

	return result;
}

/******************************************************************************
 * For the predelete command: delete everything a following update run would  *
 * delete. (Assuming no unexpected errors occur, like a file missing upstream.*
 *****************************************************************************/

retvalue updates_predelete(struct update_distribution *distributions, bool nolistsdownload, bool skipold) {
	retvalue result, r;
	struct update_distribution *d;
	struct aptmethodrun *run;

	causingfile = NULL;

	result = updates_prepare(distributions, true, nolistsdownload, skipold,
			&run);
	if (!RET_IS_OK(result))
		return result;

	if (verbose > 0)
		printf("Shutting down aptmethods...\n");
	r = aptmethod_shutdown(run);
	RET_UPDATE(result, r);
	if (RET_WAS_ERROR(result)) {
		return result;
	}

	if (verbose >= 0)
		printf("Removing obsolete or to be replaced packages...\n");
	for (d=distributions ; d != NULL ; d=d->next) {
		struct distribution *dd = d->distribution;
		struct update_target *u;

		for (u=d->targets ; u != NULL ; u=u->next) {
			r = searchformissing(stdout, u);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r)) {
				u->incomplete = true;
				continue;
			}
			if (u->nothingnew || u->ignoredelete) {
				upgradelist_free(u->upgradelist);
				u->upgradelist = NULL;
				continue;
			}
			r = upgradelist_predelete(u->upgradelist, dd->logger);
			RET_UPDATE(dd->status, r);
			if (RET_WAS_ERROR(r))
				u->incomplete = true;
			RET_UPDATE(result, r);
			upgradelist_free(u->upgradelist);
			u->upgradelist = NULL;
			if (RET_WAS_ERROR(r))
				return r;
			if (RET_IS_OK(result) && dd->tracking != dt_NONE) {
				r = tracking_retrack(dd, false);
				RET_ENDUPDATE(result, r);
			}
		}
	}
	logger_wait();
	return result;
}

/******************************************************************************
 * The cleanlists command has to mark all files that might be scheduled to be *
 * downloaded again, so that the rest can be deleted                          *
 ******************************************************************************/

static void marktargetsneeded(struct cachedlistfile *files, const struct distribution *d, component_t flat, /*@null@*/const struct strlist *a_from, /*@null@*/const struct strlist *a_into, /*@null@*/const struct strlist *c_from, /*@null@*/const struct strlist *uc_from, const char *repository, const char *suite) {
	struct target *t;
	int i, ai;

	if (atom_defined(flat)) {
		bool deb_needed = false, dsc_needed = false;

		for (t = d->targets ; t != NULL ; t = t->next) {
			if (t->packagetype == pt_udeb)
				continue;
			if (flat != t->architecture)
				continue;
			if (a_into != NULL &&
					!strlist_in(a_into,
						atoms_architectures[
						t->architecture]))
				continue;
			if (t->packagetype == pt_deb)
				deb_needed = true;
			else if (t->packagetype == pt_dsc)
				dsc_needed = true;
		}
		if (deb_needed)
			cachedlistfile_need_flat_index(files,
					repository, suite, pt_deb);
		if (dsc_needed)
			cachedlistfile_need_flat_index(files,
					repository, suite, pt_dsc);
		return;
	}
	/* .dsc */
	if ((a_into != NULL && strlist_in(a_into, "source")) ||
			(a_into == NULL && atomlist_in(&d->architectures,
						       architecture_source))) {
		if (c_from != NULL)
			for (i = 0 ; i < c_from->count ; i++)
				cachedlistfile_need_index(files,
						repository, suite, "source",
						c_from->values[i], pt_dsc);
		else
			for (i = 0 ; i < d->components.count ; i++)
				cachedlistfile_need_index(files,
						repository, suite, "source",
						atoms_components[
						 d->components.atoms[i]],
						pt_dsc);
	}
	/* .deb and .udeb */
	if (a_into != NULL) {
		for (ai = 0 ; ai < a_into->count ; ai++) {
			const char *a = a_from->values[ai];

			if (strcmp(a_into->values[ai], "source") == 0)
				continue;
			if (c_from != NULL)
				for (i = 0 ; i < c_from->count ; i++)
					cachedlistfile_need_index(files,
							repository, suite, a,
							c_from->values[i],
							pt_deb);
			else
				for (i = 0 ; i < d->components.count ; i++)
					cachedlistfile_need_index(files,
							repository, suite, a,
							atoms_components[
							 d->components.atoms[i]],
							pt_deb);
			if (uc_from != NULL)
				for (i = 0 ; i < uc_from->count ; i++)
					cachedlistfile_need_index(files,
							repository, suite, a,
							uc_from->values[i],
							pt_udeb);
			else
				for (i = 0 ; i < d->udebcomponents.count ; i++)
					cachedlistfile_need_index(files,
							repository, suite, a,
							atoms_components[
							 d->components.atoms[i]],
							pt_udeb);
		}
	} else {
		for (ai = 0 ; ai < d->architectures.count ; ai++) {
			const char *a = atoms_architectures[
				d->architectures.atoms[ai]];

			if (d->architectures.atoms[ai] == architecture_source)
				continue;
			if (c_from != NULL)
				for (i = 0 ; i < c_from->count ; i++)
					cachedlistfile_need_index(files,
							repository, suite, a,
							c_from->values[i],
							pt_deb);
			else
				for (i = 0 ; i < d->components.count ; i++)
					cachedlistfile_need_index(files,
							repository, suite, a,
							atoms_components[
							 d->components.atoms[i]],
							pt_deb);
			if (uc_from != NULL)
				for (i = 0 ; i < uc_from->count ; i++)
					cachedlistfile_need_index(files,
							repository, suite, a,
							uc_from->values[i],
							pt_udeb);
			else
				for (i = 0 ; i < d->udebcomponents.count ; i++)
					cachedlistfile_need_index(files,
							repository, suite, a,
							atoms_components[
							 d->components.atoms[i]],
							pt_udeb);
		}
	}
}

retvalue updates_cleanlists(const struct distribution *distributions, const struct update_pattern *patterns) {
	retvalue result;
	const struct distribution *d;
	const struct update_pattern *p, *q;
	struct cachedlistfile *files;
	int i;
	bool isflat;
	const struct strlist *uc_from = NULL;
	const struct strlist *c_from = NULL;
	const struct strlist *a_from = NULL, *a_into = NULL;
	const char *repository;
	char *suite;

	result = cachedlists_scandir(&files);
	if (!RET_IS_OK(result))
		return result;

	result = RET_OK;
	for (d = distributions ; d != NULL ; d = d->next) {
		if (d->updates.count == 0)
			continue;
		cachedlistfile_need(files, "lastseen", 2, "", d->codename, NULL);
		for (i = 0; i < d->updates.count ; i++) {
			const char *name = d->updates.values[i];

			if (strcmp(name, "-") == 0)
				continue;

			p = patterns;
			while (p != NULL && strcmp(name, p->name) != 0)
				p = p->next;
			if (p == NULL) {
				fprintf(stderr,
"Cannot find definition of upgrade-rule '%s' for distribution '%s'!\n",
						name, d->codename);
				result = RET_ERROR;
				continue;
			}
			q = p;
			while (q != NULL && q->pattern_from != NULL)
				q = q->pattern_from;
			repository = q->name;
			q = p;
			while (q != NULL && !atom_defined(q->flat))
				q = q->pattern_from;
			isflat = q != NULL;
			q = p;
			while (q != NULL && !q->architectures_set)
				q = q->pattern_from;
			if (q != NULL) {
				a_from = &q->architectures_from;
				a_into = &q->architectures_into;
			}
			q = p;
			while (q != NULL && !q->components_set)
				q = q->pattern_from;
			if (q != NULL)
				c_from = &q->components_from;
			q = p;
			while (q != NULL && !q->udebcomponents_set)
				q = q->pattern_from;
			if (q != NULL)
				uc_from = &q->udebcomponents_from;
			suite = translate_suite_pattern(p, d->codename);
			if (FAILEDTOALLOC(suite)) {
				cachedlistfile_freelist(files);
				return RET_ERROR_OOM;
			}
			/* Only index files are intresting, everything else
			 * Release, Release.gpg, compressed files, hook processed
			 * files is deleted */
			marktargetsneeded(files, d, isflat, a_from, a_into,
					c_from, uc_from, repository, suite);
			free(suite);

		}
	}
	cachedlistfile_deleteunneeded(files);
	cachedlistfile_freelist(files);
	return RET_OK;
}
