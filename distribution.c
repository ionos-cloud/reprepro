/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007 Bernhard R. Link
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
#include <limits.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "error.h"
#include "mprintf.h"
#include "chunks.h"
#include "sources.h"
#include "dirs.h"
#include "names.h"
#include "release.h"
#include "tracking.h"
#include "override.h"
#include "log.h"
#include "ignore.h"
#include "uploaderslist.h"
#include "configparser.h"
#include "distribution.h"

extern int verbose;

static retvalue distribution_free(struct distribution *distribution) {
	retvalue result,r;

	if( distribution != NULL ) {
		free(distribution->codename);
		free(distribution->suite);
		free(distribution->version);
		free(distribution->origin);
		free(distribution->notautomatic);
		free(distribution->label);
		free(distribution->description);
		free(distribution->signwith);
		free(distribution->deb_override);
		free(distribution->udeb_override);
		free(distribution->dsc_override);
		free(distribution->uploaders);
		strlist_done(&distribution->udebcomponents);
		strlist_done(&distribution->architectures);
		strlist_done(&distribution->components);
		strlist_done(&distribution->updates);
		strlist_done(&distribution->pulls);
		strlist_done(&distribution->alsoaccept);
		exportmode_done(&distribution->dsc);
		exportmode_done(&distribution->deb);
		exportmode_done(&distribution->udeb);
		strlist_done(&distribution->contents_architectures);
		strlist_done(&distribution->contents_components);
		strlist_done(&distribution->contents_ucomponents);
		override_free(distribution->overrides.deb);
		override_free(distribution->overrides.udeb);
		override_free(distribution->overrides.dsc);
		logger_free(distribution->logger);
		if( distribution->uploaderslist != NULL ) {
			uploaders_unlock(distribution->uploaderslist);
		}
		result = RET_OK;

		while( distribution->targets != NULL ) {
			struct target *next = distribution->targets->next;

			r = target_free(distribution->targets);
			RET_UPDATE(result,r);
			distribution->targets = next;
		}
		free(distribution);
		return result;
	} else
		return RET_OK;
}

/* allow premature free'ing of overrides to save some memory */
void distribution_unloadoverrides(struct distribution *distribution) {
	override_free(distribution->overrides.deb);
	override_free(distribution->overrides.udeb);
	override_free(distribution->overrides.dsc);
	distribution->overrides.deb = NULL;
	distribution->overrides.udeb = NULL;
	distribution->overrides.dsc = NULL;
}

/* create all contained targets... */
static retvalue createtargets(struct distribution *distribution) {
	retvalue r;
	int i,j;
	const char *arch,*comp;
	struct target *t;
	struct target *last = NULL;

	for( i = 0 ; i < distribution->components.count ; i++ ) {
		comp = distribution->components.values[i];
		for( j = 0 ; j < distribution->architectures.count ; j++ ) {
			arch = distribution->architectures.values[j];
			if( strcmp(arch,"source") != 0 ) {
				if( strcmp(arch,"all") == 0 && verbose >= 0 ) {
					fprintf(stderr,
"WARNING: Distribution %s contains an architecture called 'all'.\n",
						distribution->codename);
				}

				r = target_initialize_binary(distribution->codename,comp,arch,&distribution->deb,&t);
				if( RET_IS_OK(r) ) {
					if( last != NULL ) {
						last->next = t;
					} else {
						distribution->targets = t;
					}
					last = t;
				}
				if( RET_WAS_ERROR(r) )
					return r;
				if( strlist_in(&distribution->udebcomponents,comp) ) {
					r = target_initialize_ubinary(distribution->codename,comp,arch,&distribution->udeb,&t);
					if( RET_IS_OK(r) ) {
						if( last != NULL ) {
							last->next = t;
						} else {
							distribution->targets = t;
						}
						last = t;
					}
					if( RET_WAS_ERROR(r) )
						return r;

				}
			}

		}
		/* check if this distribution contains source
		 * (yes, yes, source is not really an architecture, but
		 *  the .changes files started with this...) */
		if( strlist_in(&distribution->architectures,"source") ) {
			r = target_initialize_source(distribution->codename,comp,&distribution->dsc,&t);
			if( last != NULL ) {
				last->next = t;
			} else {
				distribution->targets = t;
			}
			last = t;
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return RET_OK;
}

struct read_distribution_data {
	const char *logdir;
	struct distribution *distributions;
};

CFstartparse(distribution) {
	CFstartparseVAR(distribution, result_p);
	struct distribution *n;
	retvalue r;

	n = calloc(1, sizeof(struct distribution));
	if( n == NULL )
		return RET_ERROR_OOM;
	/* set some default value: */
	r = exportmode_init(&n->udeb, true, NULL, "Packages");
	if( RET_WAS_ERROR(r) ) {
		(void)distribution_free(n);
		return r;
	}
	r = exportmode_init(&n->deb, true, "Release", "Packages");
	if( RET_WAS_ERROR(r) ) {
		(void)distribution_free(n);
		return r;
	}
	r = exportmode_init(&n->dsc, false, "Release", "Sources");
	if( RET_WAS_ERROR(r) ) {
		(void)distribution_free(n);
		return r;
	}
	*result_p = n;
	return RET_OK;
}

static bool notpropersuperset(const struct strlist *allowed, const char *allowedname,
		const struct strlist *check, const char *checkname,
		const struct configiterator *iter, const struct distribution *d) {
	const char *missing;

	if( !strlist_subset(allowed, check, &missing) ) {
		fprintf(stderr,
"In distribution description of '%s' (line %u to %u in %s):\n"
"%s contains '%s' not found in %s!\n",
				d->codename,
				d->firstline, d->lastline,
				config_filename(iter),
				checkname, missing, allowedname);
		return true;
	}
	return false;
}

CFfinishparse(distribution) {
	CFfinishparseVARS(distribution,n,last_p,mydata);
	struct distribution *d;
	retvalue r;

	if( !complete ) {
		distribution_free(n);
		return RET_NOTHING;
	}
	n->firstline = config_firstline(iter);
	n->lastline = config_line(iter) - 1;

	/* Do some consitency checks */
	for( d = mydata->distributions; d != NULL; d = d->next ) {
		if( strcmp(d->codename, n->codename) == 0 ) {
			fprintf(stderr,
"Multiple distributions with the common codename: '%s'!\n"
"First was in %s line %u to %u, another in lines %u to %u",
				n->codename, config_filename(iter),
				d->firstline, d->lastline,
				n->firstline, n->lastline);
			distribution_free(n);
			return RET_ERROR;
		}
	}

	if( notpropersuperset(&n->architectures, "Architectures",
			    &n->contents_architectures, "ContentsArchitectures",
			    iter, n) ||
	    notpropersuperset(&n->components, "Components",
			    &n->contents_components, "ContentsComponents",
			    iter, n) ||
	    notpropersuperset(&n->udebcomponents, "UDebComponents",
			    &n->contents_ucomponents, "ContentsUComponents",
			    iter, n) ||
	    // TODO: instead of checking here make sure it can have more
	    // in the rest of the code...:
	    notpropersuperset(&n->components, "Components",
			    &n->udebcomponents, "UDebComponents",
			    iter, n) ) {
		(void)distribution_free(n);
		return RET_ERROR;
	}
	/* overwrite creation of contents files based on given lists: */
	if( n->contents_components_set ) {
		if ( n->contents_components.count > 0 ) {
			n->contents.flags.enabled = true;
			n->contents.flags.nodebs = false;
		} else {
			n->contents.flags.nodebs = true;
		}
	}
	if( n->contents_ucomponents_set ) {
		if ( n->contents_ucomponents.count > 0 ) {
			n->contents.flags.enabled = true;
			n->contents.flags.udebs = true;
		} else {
			n->contents.flags.udebs = false;
		}
	}
	if( n->contents_architectures_set ) {
		if( n->contents_architectures.count > 0 )
			n->contents.flags.enabled = true;
		else
			n->contents.flags.enabled = false;
	}
	/* prepare substructures */

	r = createtargets(n);
	if( RET_WAS_ERROR(r) ) {
		(void)distribution_free(n);
		return r;
	}
	n->status = RET_NOTHING;
	n->lookedat = false;
	n->selected = false;

	/* put in linked list */
	if( *last_p == NULL )
		mydata->distributions = n;
	else
		(*last_p)->next = n;
	*last_p = n;
	return RET_OK;
}

CFallSETPROC(distribution, suite)
CFallSETPROC(distribution, version)
CFallSETPROC(distribution, origin)
CFallSETPROC(distribution, notautomatic)
CFallSETPROC(distribution, label)
CFallSETPROC(distribution, description)
CFkeySETPROC(distribution, signwith)
CFfileSETPROC(distribution, deb_override)
CFfileSETPROC(distribution, udeb_override)
CFfileSETPROC(distribution, dsc_override)
CFfileSETPROC(distribution, uploaders)
CFuniqstrlistSETPROC(distribution, udebcomponents)
CFuniqstrlistSETPROC(distribution, alsoaccept)
CFstrlistSETPROC(distribution, updates)
CFstrlistSETPROC(distribution, pulls)
CFuniqstrlistSETPROCset(distribution, contents_architectures)
CFuniqstrlistSETPROCset(distribution, contents_components)
CFuniqstrlistSETPROCset(distribution, contents_ucomponents)
CFexportmodeSETPROC(distribution, udeb)
CFexportmodeSETPROC(distribution, deb)
CFexportmodeSETPROC(distribution, dsc)
CFcheckuniqstrlistSETPROC(distribution, components, checkforcomponent)
CFcheckuniqstrlistSETPROC(distribution, architectures, checkforarchitecture)
CFcheckvalueSETPROC(distribution, codename, checkforcodename)

CFUSETPROC(distribution, Contents) {
	CFSETPROCVAR(distribution, d);
	return contentsoptions_parse(d, iter);
}
CFuSETPROC(distribution, logger) {
	CFSETPROCVARS(distribution, d, mydata);
	return logger_init(confdir, mydata->logdir, iter, &d->logger);
}
CFUSETPROC(distribution, Tracking) {
	CFSETPROCVAR(distribution, d);
	return tracking_parse(d, iter);
}

static const struct configfield distributionconfigfields[] = {
	CF("AlsoAcceptFor",	distribution,	alsoaccept),
	CFr("Architectures",	distribution,	architectures),
	CFr("Codename",		distribution,	codename),
	CFr("Components",	distribution,	components),
	CF("ContentsArchitectures", distribution, contents_architectures),
	CF("ContentsComponents", distribution,	contents_components),
	CF("Contents",		distribution,	Contents),
	CF("ContentsUComponents", distribution,	contents_ucomponents),
	CF("DebIndices",	distribution,	deb),
	CF("DebOverride",	distribution,	deb_override),
	CF("Description",	distribution,	description),
	CF("DscIndices",	distribution,	dsc),
	CF("DscOverride",	distribution,	dsc_override),
	CF("Label",		distribution,	label),
	CF("Log",		distribution,	logger),
	CF("NotAutomatic",	distribution,	notautomatic),
	CF("Origin",		distribution,	origin),
	CF("Pull",		distribution,	pulls),
	CF("SignWith",		distribution,	signwith),
	CF("Suite",		distribution,	suite),
	CF("Tracking",		distribution,	Tracking),
	CF("UDebComponents",	distribution,	udebcomponents),
	CF("UDebIndices",	distribution,	udeb),
	CF("UDebOverride",	distribution,	udeb_override),
	CF("Update",		distribution,	updates),
	CF("Uploaders",		distribution,	uploaders),
	CF("Version",		distribution,	version)
};

/* read specification of all distributions */
retvalue distribution_readall(const char *confdir, const char *logdir, struct distribution **distributions) {
	struct read_distribution_data mydata;
	retvalue result;

	mydata.logdir = logdir;
	mydata.distributions = NULL;

	// TODO: readd some way to tell about -b or --confdir here?
	/*
	result = regularfileexists(fn);
	if( RET_WAS_ERROR(result) ) {
		fprintf(stderr,"Could not find '%s'!\n"
"(Have you forgotten to specify a basedir by -b?\n"
"To only set the conf/ dir use --confdir)\n",fn);
		free(mydata.filter.found);
		free(fn);
		return RET_ERROR_MISSING;
	}
	*/

	result = configfile_parse(confdir, "distributions",
			IGNORABLE(unknownfield),
			startparsedistribution, finishparsedistribution,
			distributionconfigfields,
			ARRAYCOUNT(distributionconfigfields),
			&mydata);
	if( result == RET_ERROR_UNKNOWNFIELD )
		fprintf(stderr, "Use --ignore=unknownfield to ignore unknown fields\n");
	if( RET_WAS_ERROR(result) ) {
		distribution_freelist(mydata.distributions);
		return result;
	}
	if( mydata.distributions == NULL ) {
		fprintf(stderr, "No distribution definitions found in %s/distributions!\n",
				confdir);
		distribution_freelist(mydata.distributions);
		return RET_ERROR_MISSING;
	}
	*distributions = mydata.distributions;
	return RET_OK;
}

/* call <action> for each part of <distribution>, within initpackagesdb/closepackagesdb */
retvalue distribution_foreach_rwopenedpart(struct distribution *distribution,struct database *database,const char *component,const char *architecture,const char *packagetype,distribution_each_action action,void *data) {
	retvalue result,r;
	struct target *t;

	result = RET_NOTHING;
	for( t = distribution->targets ; t != NULL ; t = t->next ) {
		if( !target_matches(t, component, architecture, packagetype) )
			continue;
		r = target_initpackagesdb(t, database, READWRITE);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			return result;
		r = action(data, t, distribution);
		RET_UPDATE(result, r);
		// TODO: how to seperate this in those affecting distribution
		// and those that do not?
		RET_UPDATE(distribution->status, r);
		r = target_closepackagesdb(t);
		RET_UPDATE(distribution->status, r);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(result) )
			return result;
	}
	return result;
}

/* call <action> for each part of <distribution>, within initpackagesdb/closepackagesdb */
retvalue distribution_foreach_roopenedpart(struct distribution *distribution,struct database *database,const char *component,const char *architecture,const char *packagetype,distribution_each_action action,void *data) {
	retvalue result,r;
	struct target *t;

	result = RET_NOTHING;
	for( t = distribution->targets ; t != NULL ; t = t->next ) {
		if( !target_matches(t, component, architecture, packagetype) )
			continue;
		r = target_initpackagesdb(t, database, READONLY);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			return result;
		r = action(data, t, distribution);
		RET_UPDATE(result, r);
		r = target_closepackagesdb(t);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(result) )
			return result;
	}
	return result;
}

/* call <action> for each package */
retvalue distribution_foreach_package(struct distribution *distribution, struct database *database, const char *component, const char *architecture, const char *packagetype, each_package_action action, each_target_action target_action, void *data) {
	retvalue result,r;
	struct target *t;
	struct cursor *cursor;
	const char *package, *control;

	result = RET_NOTHING;
	for( t = distribution->targets ; t != NULL ; t = t->next ) {
		if( !target_matches(t, component, architecture, packagetype) )
			continue;
		if( target_action != NULL ) {
			r = target_action(database, distribution, t, data);
			if( RET_WAS_ERROR(r) )
				return result;
			if( r == RET_NOTHING )
				continue;
		}
		r = target_initpackagesdb(t, database, READONLY);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			return result;
		r = table_newglobalcursor(t->packages, &cursor);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			(void)target_closepackagesdb(t);
			return r;
		}
		while( cursor_nexttemp(t->packages, cursor,
					&package, &control) ) {
			r = action(database, distribution, t,
					package, control, data);
			RET_UPDATE(result, r);
			if( RET_WAS_ERROR(r) )
				break;
		}
		r = cursor_close(t->packages, cursor);
		RET_ENDUPDATE(result, r);
		r = target_closepackagesdb(t);
		RET_ENDUPDATE(result, r);
		if( RET_WAS_ERROR(result) )
			return result;
	}
	return result;
}

retvalue distribution_foreach_package_c(struct distribution *distribution, struct database *database, const struct strlist *components, const char *architecture, const char *packagetype, each_package_action action, void *data) {
	retvalue result,r;
	struct target *t;
	struct cursor *cursor;
	const char *package, *control;

	result = RET_NOTHING;
	for( t = distribution->targets ; t != NULL ; t = t->next ) {
		if( components != NULL && !strlist_in(components, t->component) )
			continue;
		if( architecture != NULL && strcmp(architecture,t->architecture) != 0 )
			continue;
		if( packagetype != NULL && strcmp(packagetype,t->packagetype) != 0 )
			continue;
		r = target_initpackagesdb(t, database, READONLY);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			return result;
		r = table_newglobalcursor(t->packages, &cursor);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			(void)target_closepackagesdb(t);
			return r;
		}
		while( cursor_nexttemp(t->packages, cursor,
					&package, &control) ) {
			r = action(database, distribution, t,
					package, control, data);
			RET_UPDATE(result, r);
			if( RET_WAS_ERROR(r) )
				break;
		}
		r = cursor_close(t->packages, cursor);
		RET_ENDUPDATE(result, r);
		r = target_closepackagesdb(t);
		RET_ENDUPDATE(result, r);
		if( RET_WAS_ERROR(result) )
			return result;
	}
	return result;
}

struct target *distribution_gettarget(const struct distribution *distribution,const char *component,const char *architecture,const char *packagetype) {
	struct target *t = distribution->targets;

	// TODO: think about making read only access and only alowing readwrite when lookedat is set

	while( t != NULL && ( strcmp(t->component,component) != 0 || strcmp(t->architecture,architecture) != 0 || strcmp(t->packagetype,packagetype) != 0 )) {
		t = t->next;
	}
	return t;
}

struct target *distribution_getpart(const struct distribution *distribution,const char *component,const char *architecture,const char *packagetype) {
	struct target *t = distribution->targets;

	while( t != NULL && ( strcmp(t->component,component) != 0 || strcmp(t->architecture,architecture) != 0 || strcmp(t->packagetype,packagetype) != 0 )) {
		t = t->next;
	}
	if( t == NULL ) {
		fprintf(stderr, "Internal error in distribution_getpart: Bogus request for c='%s' a='%s' t='%s' in '%s'!\n",
				component, architecture, packagetype,
				distribution->codename);
		abort();
	}
	return t;
}

/* mark all distributions matching one of the first argc argv */
retvalue distribution_match(struct distribution *alldistributions, int argc, const char *argv[], bool lookedat) {
	struct distribution *d;
	bool found[argc], unusable_as_suite[argc];
	struct distribution *has_suite[argc];
	int i;

	assert( alldistributions != NULL );

	if( argc <= 0 ) {
		for( d = alldistributions ; d != NULL ; d = d->next ) {
			d->selected = true;
			d->lookedat = lookedat;
		}
		return RET_OK;
	}
	memset(found, 0, sizeof(found));
	memset(unusable_as_suite, 0, sizeof(unusable_as_suite));
	memset(has_suite, 0, sizeof(has_suite));

	for( d = alldistributions ; d != NULL ; d = d->next ) {
		for( i = 0 ; i < argc ; i++ ) {
			if( strcmp(argv[i], d->codename) == 0 ) {
				assert( !found[i] );
				found[i] = true;
				d->selected = true;
				if( lookedat )
					d->lookedat = lookedat;
			} else if( d->suite != NULL &&
					strcmp(argv[i], d->suite) == 0 ) {
				if( has_suite[i] != NULL )
					unusable_as_suite[i] = true;
				has_suite[i] = d;
			}
		}
	}
	for( i = 0 ; i < argc ; i++ ) {
		if( !found[i] ) {
			if( has_suite[i] != NULL && !unusable_as_suite[i] ) {
				has_suite[i]->selected = true;
				if( lookedat )
					has_suite[i]->lookedat = lookedat;
				continue;
			}
			fprintf(stderr, "No distribution definition of '%s' found in distributions'!\n", argv[i]);
			if( unusable_as_suite[i] )
				fprintf(stderr,
"(It is not the codename of any distribution and there are multiple\n"
"distributions with this as suite name.)\n");
			return RET_ERROR_MISSING;
		}
	}
	return RET_OK;
}

retvalue distribution_get(struct distribution *alldistributions, const char *name, bool lookedat, struct distribution **distribution) {
	struct distribution *d, *d2;

	d = alldistributions;
	while( d != NULL && strcmp(name, d->codename) != 0 )
		d = d->next;
	if( d == NULL ) {
		for( d2 = alldistributions; d2 != NULL ; d2 = d2->next ) {
			if( d2->suite == NULL )
				continue;
			if( strcmp(name, d2->suite) != 0 )
				continue;
			if( d != NULL ) {
				fprintf(stderr,
"No distribution has '%s' as codename, but multiple as suite name,\n"
"thus it cannot be used to determine a distribution.\n", name);
				return RET_ERROR_MISSING;
			}
			d = d2;
		}
	}
	if( d == NULL ) {
		fprintf(stderr, "Cannot find definition of distribution '%s'!\n", name);
		return RET_ERROR_MISSING;
	}
	d->selected = true;
	if( lookedat )
		d->lookedat = true;
	*distribution = d;
	return RET_OK;
}

retvalue distribution_snapshot(struct distribution *distribution, const char *distdir, struct database *database, const char *name) {
	struct target *target;
	retvalue result,r;
	struct release *release;
	char *id;

	assert( distribution != NULL );

	r = release_initsnapshot(distdir,distribution->codename,name,&release);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;
	for( target=distribution->targets; target != NULL ; target = target->next ) {
		r = release_mkdir(release, target->relativedirectory);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		r = target_export(target, database, false, true, release);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		if( target->exportmode->release != NULL ) {
			r = release_directorydescription(release, distribution, target, target->exportmode->release, false);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
		}
	}
	if( !RET_WAS_ERROR(result) ) {
		result = release_prepare(release, distribution, false);
		assert( result != RET_NOTHING );
	}
	if( RET_WAS_ERROR(result) ) {
		release_free(release);
		return result;
	}
	result = release_finish(release, distribution);
	if( RET_WAS_ERROR(result) )
		return r;
	id = mprintf("s=%s=%s", distribution->codename, name);
	if( id == NULL )
		return RET_ERROR_OOM;
	r = distribution_foreach_package(distribution, database,
			NULL, NULL, NULL,
			package_referenceforsnapshot, NULL, id);
	free(id);
	RET_UPDATE(result,r);
	return result;
}

static retvalue export(struct distribution *distribution, const char *distdir, struct database *database, bool onlyneeded) {
	struct target *target;
	retvalue result,r;
	struct release *release;

	assert( distribution != NULL );

	r = release_init(&release, database, distdir, distribution->codename);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;
	for( target=distribution->targets; target != NULL ; target = target->next ) {
		r = release_mkdir(release, target->relativedirectory);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		r = target_export(target, database, onlyneeded, false, release);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		if( target->exportmode->release != NULL ) {
			r = release_directorydescription(release,distribution,target,target->exportmode->release,onlyneeded);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
		}
	}
	if( !RET_WAS_ERROR(result) && distribution->contents.flags.enabled ) {
		r = contents_generate(database, distribution,
				release, onlyneeded);
	}
	if( !RET_WAS_ERROR(result) ) {
		result = release_prepare(release,distribution,onlyneeded);
		if( result == RET_NOTHING ) {
			release_free(release);
			return result;
		}
	}
	if( RET_WAS_ERROR(result) ) {
		bool workleft = false;
		release_free(release);
		fprintf(stderr, "ERROR: Could not finish exporting '%s'!\n", distribution->codename);
		for( target=distribution->targets; target != NULL ; target = target->next ) {
			workleft |= target->saved_wasmodified;
		}
		if( workleft ) {
			(void)fputs(
"This means that from outside your repository will still look like before (and\n"
"should still work if this old state worked), but the changes intended with this\n"
"call will not be visible until you call export directly (via reprepro export)\n"
"Changes will also get visible when something else changes the same file and\n"
"thus creates a new export of that file, but even changes to other parts of the\n"
"same distribution will not!\n",	stderr);
		}
	} else {
		r = release_finish(release,distribution);
		RET_UPDATE(result,r);
	}
	if( RET_IS_OK(result) )
		distribution->status = RET_NOTHING;
	return result;
}

retvalue distribution_fullexport(struct distribution *distribution, const char *distdir, struct database *database) {
	return export(distribution, distdir, database, false);
}

retvalue distribution_freelist(struct distribution *distributions) {
	retvalue result,r;

	result = RET_NOTHING;
	while( distributions != NULL ) {
		struct distribution *d = distributions->next;
		r = distribution_free(distributions);
		RET_UPDATE(result,r);
		distributions = d;
	}
	return result;
}

retvalue distribution_exportlist(enum exportwhen when, struct distribution *distributions, const char *distdir, struct database *database) {
	retvalue result,r;
	bool todo = false;
	struct distribution *d;

	if( when == EXPORT_NEVER ) {
		if( verbose > 10 )
			fprintf(stderr, "Not exporting anything as --export=never specified\n");
		return distribution_freelist(distributions);
	}

	for( d=distributions; d != NULL; d = d->next ) {
		if( !d->selected )
			continue;
		if( d->lookedat && (RET_IS_OK(d->status) ||
			( d->status == RET_NOTHING && when != EXPORT_CHANGED) ||
			when == EXPORT_FORCE)) {
			todo = true;
		}
	}

	if( verbose >= 0 && todo )
		printf("Exporting indices...\n");

	result = RET_NOTHING;
	for( d=distributions; d != NULL; d = d->next ) {
		if( !d->selected )
			continue;
		if( !d->lookedat ) {
			if( verbose >= 30 )
				printf(
" Not exporting %s because not looked at.\n", d->codename);
		} else if( (RET_WAS_ERROR(d->status)||interrupted()) &&
		           when != EXPORT_FORCE ) {
			if( verbose >= 10 )
				fprintf(stderr,
" Not exporting %s because there have been errors and no --export=force.\n",
						d->codename);
		} else if( d->status==RET_NOTHING && when==EXPORT_CHANGED ) {
			struct target *t;

			if( verbose >= 10 )
				printf(
" Not exporting %s because of no recorded changes and --export=changed.\n",
						d->codename);

			/* some paranoid check */

			for( t = d->targets ; t != NULL ; t = t->next ) {
				if( t->wasmodified ) {
					fprintf(stderr,
"A paranoid check found distribution %s would not have been exported,\n"
"despite having parts that are marked changed by deeper code.\n"
"Please report this and how you got this message as bugreport. Thanks.\n"
"Doing a export despite --export=changed....\n",
						d->codename);
					r = export(d, distdir, database, true);
					RET_UPDATE(result,r);
					break;
				}
			}
		} else {
			assert( RET_IS_OK(d->status) ||
					( d->status == RET_NOTHING &&
					  when != EXPORT_CHANGED) ||
					when == EXPORT_FORCE);
			r = export(d, distdir, database, true);
			RET_UPDATE(result,r);
		}
	}
	return result;
}

retvalue distribution_export(enum exportwhen when, struct distribution *distribution, const char *distdir, struct database *database) {
	if( when == EXPORT_NEVER ) {
		if( verbose >= 10 )
			fprintf(stderr,
"Not exporting %s because of --export=never.\n"
"Make sure to run a full export soon.\n", distribution->codename);
		return RET_NOTHING;
	}
	if( when != EXPORT_FORCE && (RET_WAS_ERROR(distribution->status)||interrupted()) ) {
		if( verbose >= 10 )
			fprintf(stderr,
"Not exporting %s because there have been errors and no --export=force.\n"
"Make sure to run a full export soon.\n", distribution->codename);
		return RET_NOTHING;
	}
	if( when == EXPORT_CHANGED && distribution->status == RET_NOTHING ) {
		struct target *t;

		if( verbose >= 10 )
			fprintf(stderr,
"Not exporting %s because of no recorded changes and --export=changed.\n",
	distribution->codename);

		/* some paranoid check */

		for( t = distribution->targets ; t != NULL ; t = t->next ) {
			if( t->wasmodified ) {
				fprintf(stderr,
"A paranoid check found distribution %s would not have been exported,\n"
"despite having parts that are marked changed by deeper code.\n"
"Please report this and how you got this message as bugreport. Thanks.\n"
"Doing a export despite --export=changed....\n",
						distribution->codename);
				return export(distribution, distdir,
						database, true);
				break;
			}
		}

		return RET_NOTHING;
	}
	if( verbose >= 0 )
		printf("Exporting indices...\n");
	return export(distribution, distdir, database, true);
}

/* get a pointer to the apropiate part of the linked list */
struct distribution *distribution_find(struct distribution *distributions, const char *name) {
	struct distribution *d = distributions, *r;

	while( d != NULL && strcmp(d->codename, name) != 0 )
		d = d->next;
	if( d != NULL )
		return d;
	d = distributions;
	while( d != NULL && !strlist_in(&d->alsoaccept, name) )
		d = d->next;
	r = d;
	if( r != NULL ) {
		while( d != NULL && ! strlist_in(&d->alsoaccept, name) )
			d = d->next;
		if( d == NULL )
			return r;
		fprintf(stderr, "No distribution has codename '%s' and multiple have it in AlsoAcceptFor!\n", name);
		return NULL;
	}
	d = distributions;
	while( d != NULL && ( d->suite == NULL || strcmp(d->suite, name) != 0 ))
		d = d->next;
	r = d;
	if( r == NULL ) {
		fprintf(stderr, "No distribution named '%s' found!\n", name);
		return NULL;
	}
	while( d != NULL && ( d->suite == NULL || strcmp(d->suite, name) != 0 ))
		d = d->next;
	if( d == NULL )
		return r;
	fprintf(stderr, "No distribution has codename '%s' and multiple have it as suite-name!\n", name);
	return NULL;
}

retvalue distribution_loadalloverrides(struct distribution *distribution, const char *confdir, const char *overridedir) {
	retvalue r;

	if( distribution->overrides.deb == NULL ) {
		r = override_read(confdir, overridedir, distribution->deb_override, &distribution->overrides.deb);
		if( RET_WAS_ERROR(r) ) {
			distribution->overrides.deb = NULL;
			return r;
		}
	}
	if( distribution->overrides.udeb == NULL ) {
		r = override_read(confdir, overridedir, distribution->udeb_override, &distribution->overrides.udeb);
		if( RET_WAS_ERROR(r) ) {
			distribution->overrides.udeb = NULL;
			return r;
		}
	}
	if( distribution->overrides.dsc == NULL ) {
		r = override_read(confdir, overridedir, distribution->dsc_override, &distribution->overrides.dsc);
		if( RET_WAS_ERROR(r) ) {
			distribution->overrides.dsc = NULL;
			return r;
		}
	}
	if( distribution->overrides.deb != NULL ||
	    distribution->overrides.udeb != NULL ||
	    distribution->overrides.dsc != NULL )
		return RET_OK;
	else
		return RET_NOTHING;
}

retvalue distribution_loaduploaders(struct distribution *distribution, const char *confdir) {
	if( distribution->uploaders != NULL ) {
		if( distribution->uploaderslist != NULL )
			return RET_OK;
		return uploaders_get(&distribution->uploaderslist,
				confdir, distribution->uploaders);
	} else {
		distribution->uploaderslist = NULL;
		return RET_NOTHING;
	}
}

void distribution_unloaduploaders(struct distribution *distribution) {
	if( distribution->uploaderslist != NULL ) {
		uploaders_unlock(distribution->uploaderslist);
		distribution->uploaderslist = NULL;
	}
}

retvalue distribution_prepareforwriting(struct distribution *distribution) {
	retvalue r;

	if( distribution->logger != NULL ) {
		r = logger_prepare(distribution->logger);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	distribution->lookedat = true;
	return RET_OK;
}

/* delete every package decider returns RET_OK for */
retvalue distribution_remove_packages(struct distribution *distribution, struct database *database, const char *component, const char *architecture, const char *packagetype, each_package_action decider, struct strlist *dereferenced, struct trackingdata *trackingdata, void *data) {
	retvalue result,r;
	struct target *t;
	struct cursor *cursor;
	const char *package, *control;

	result = RET_NOTHING;
	for( t = distribution->targets ; t != NULL ; t = t->next ) {
		if( component != NULL && strcmp(component,t->component) != 0 )
			continue;
		if( architecture != NULL && strcmp(architecture,t->architecture) != 0 )
			continue;
		if( packagetype != NULL && strcmp(packagetype,t->packagetype) != 0 )
			continue;
		r = target_initpackagesdb(t, database, READWRITE);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			return result;
		r = table_newglobalcursor(t->packages, &cursor);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			(void)target_closepackagesdb(t);
			return r;
		}
		while( cursor_nexttemp(t->packages, cursor,
					&package, &control) ) {
			r = decider(database, distribution, t,
					package, control, data);
			RET_UPDATE(result, r);
			if( RET_WAS_ERROR(r) )
				break;
			if( RET_IS_OK(r) ) {
				r = target_removepackage_by_cursor(t,
					distribution->logger, database, cursor,
					package, control, NULL, dereferenced,
					trackingdata);
				RET_UPDATE(result, r);
				RET_UPDATE(distribution->status, r);
			}
		}
		r = cursor_close(t->packages, cursor);
		RET_ENDUPDATE(result, r);
		r = target_closepackagesdb(t);
		RET_ENDUPDATE(result, r);
		if( RET_WAS_ERROR(result) )
			return result;
	}
	return result;
}
