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
#include <zlib.h>
#include "error.h"
#include "mprintf.h"
#include "chunks.h"
#include "sources.h"
#include "md5sum.h"
#include "dirs.h"
#include "names.h"
#include "release.h"
#include "copyfile.h"
#include "tracking.h"
#include "override.h"
#include "log.h"
#include "ignore.h"
#include "uploaderslist.h"
#include "configparser.h"
#include "distribution.h"

extern int verbose;

retvalue distribution_free(struct distribution *distribution) {
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
		contentsoptions_done(&distribution->contents);
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

/* allow premature free'ing of overrides to save some memorty */
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
			    &n->contents.architectures, "ContentsArchitectures",
			    iter, n) ||
	    notpropersuperset(&n->components, "Components",
			    &n->contents.components, "ContentsComponents",
			    iter, n) ||
	    notpropersuperset(&n->udebcomponents, "UDebComponents",
			    &n->contents.ucomponents, "ContentsUComponents",
			    iter, n) ||
	    // TODO: instead of checking here make sure it can have more
	    // in the rest of the code...
	    notpropersuperset(&n->components, "Components",
			    &n->udebcomponents, "UDebComponents",
			    iter, n) ) {
		(void)distribution_free(n);
		return RET_ERROR;
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
CFuniqstrlistSETPROCsub(distribution, contents, architectures)
CFuniqstrlistSETPROCsub(distribution, contents, components)
CFuniqstrlistSETPROCsub(distribution, contents, ucomponents)
CFexportmodeSETPROC(distribution, udeb)
CFexportmodeSETPROC(distribution, deb)
CFexportmodeSETPROC(distribution, dsc)
// TODO: readd checking these values for sanity!!!!!
CFuniqstrlistSETPROC(distribution, components)
CFuniqstrlistSETPROC(distribution, architectures)
CFvalueSETPROC(distribution, codename)

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
		fprintf(stderr, "To ignore unknown fields use --ignore=unknownfield\n");
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

/* call <action> for each part of <distribution>. */
retvalue distribution_foreach_part(struct distribution *distribution,const char *component,const char *architecture,const char *packagetype,distribution_each_action action,void *data) {
	retvalue result,r;
	struct target *t;

	result = RET_NOTHING;
	for( t = distribution->targets ; t != NULL ; t = t->next ) {
		if( component != NULL && strcmp(component,t->component) != 0 )
			continue;
		if( architecture != NULL && strcmp(architecture,t->architecture) != 0 )
			continue;
		if( packagetype != NULL && strcmp(packagetype,t->packagetype) != 0 )
			continue;
		r = action(data,t,distribution);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			return result;
	}
	return result;
}

/* call <action> for each part of <distribution>, within initpackagesdb/closepackagesdb */
retvalue distribution_foreach_rwopenedpart(struct distribution *distribution,struct database *database,const char *component,const char *architecture,const char *packagetype,distribution_each_action action,void *data) {
	retvalue result,r;
	struct target *t;

	result = RET_NOTHING;
	for( t = distribution->targets ; t != NULL ; t = t->next ) {
		if( component != NULL && strcmp(component,t->component) != 0 )
			continue;
		if( architecture != NULL && strcmp(architecture,t->architecture) != 0 )
			continue;
		if( packagetype != NULL && strcmp(packagetype,t->packagetype) != 0 )
			continue;
		r = target_initpackagesdb(t, database);
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
		if( component != NULL && strcmp(component,t->component) != 0 )
			continue;
		if( architecture != NULL && strcmp(architecture,t->architecture) != 0 )
			continue;
		if( packagetype != NULL && strcmp(packagetype,t->packagetype) != 0 )
			continue;
		r = target_initpackagesdb(t, database);
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

/* get all dists from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_getmatched(const char *confdir, const char *logdir, int argc, const char *argv[], bool lookedat, struct distribution **distributions) {
	retvalue r;
	struct distribution *d, *alldistributions, *selecteddistributions, **l;
	bool *found;
	int i;

	r = distribution_readall(confdir, logdir, &alldistributions);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;

	// TODO: only set selected on those choosen. (once reprepro is ready) */
	if( argc <= 0 ) {
		for( d = alldistributions ; d != NULL ; d = d->next ) {
			d->selected = true;
			d->lookedat = lookedat;
		}
		assert( alldistributions != NULL );
		*distributions = alldistributions;
		return RET_OK;
	}
	found = calloc(argc, sizeof(bool));
	if( found == NULL ) {
		distribution_freelist(alldistributions);
		return RET_ERROR_OOM;
	}
	selecteddistributions = NULL;
	l = &selecteddistributions;

	while( alldistributions != NULL ) {
		d = alldistributions;
		alldistributions = d->next;
		d->next = NULL;

		for( i = 0 ; i < argc ; i++ ) {
			if( strcmp(argv[i], d->codename) == 0 ) {
				assert( !found[i] );
				found[i] = true;
				d->selected = true;
				d->lookedat = lookedat;
				*l = d;
				l = &d->next;
				d = NULL;
				break;
			}
		}
		if( d != NULL )
			(void)distribution_free(d);
	}
	for( i = 0 ; i < argc ; i++ ) {
		if( !found[i] ) {
			fprintf(stderr, "No distribution definition of '%s' found in '%s/distributions'!\n", argv[i], confdir);
			distribution_freelist(selecteddistributions);
			free(found);
			return RET_ERROR_MISSING;
		}
	}
	assert( selecteddistributions != NULL );
	*distributions = selecteddistributions;
	free(found);
	return RET_OK;
}

retvalue distribution_get(const char *confdir, const char *logdir, const char *name, bool lookedat, struct distribution **distribution) {
	retvalue result;
	struct distribution *d;

	/* This is a bit overkill, as it does not stop when it finds the
	 * definition of the distribution. But this way we can warn
	 * about emtpy lines in the definition (as this would split
	 * it in two definitions, the second one no valid one).
	 */
	result = distribution_getmatched(confdir,logdir,1,&name,lookedat,&d);

	if( RET_WAS_ERROR(result) )
		return result;

	if( result == RET_NOTHING ) {
		fprintf(stderr,"Cannot find definition of distribution '%s' in %s/distributions!\n",name,confdir);
		return RET_ERROR_MISSING;
	}
	assert( d != NULL && d->next == NULL );

	*distribution = d;
	return RET_OK;
}

retvalue distribution_snapshot(struct distribution *distribution,
		const char *confdir, const char *distdir,
		struct database *database, const char *name) {
	struct target *target;
	retvalue result,r;
	struct release *release;

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
		r = target_export(target, confdir, database,
				false, true, release);
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
	if( RET_WAS_ERROR(result) ) {
		release_free(release);
		return result;
	}
	result = release_write(release, distribution, false);
	if( RET_WAS_ERROR(result) )
		return r;
	/* add references so that the pool files belonging to it are not deleted */
	for( target=distribution->targets; target != NULL ; target = target->next ) {
		r = target_addsnapshotreference(target, database, name);
		RET_UPDATE(result,r);
	}
	return result;
}

static retvalue export(struct distribution *distribution,
		const char *confdir, const char *distdir,
		struct database *database, bool onlyneeded) {
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
		r = target_export(target, confdir, database,
				onlyneeded, false, release);
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
	if( !RET_WAS_ERROR(result) && distribution->contents.rate > 0 ) {
		r = contents_generate(database, distribution,
				release, onlyneeded);
	}
	if( RET_WAS_ERROR(result) )
		release_free(release);
	else {
		retvalue r;

		r = release_write(release,distribution,onlyneeded);
		RET_UPDATE(result,r);
	}
	if( RET_IS_OK(result) )
		distribution->status = RET_NOTHING;
	return result;
}

retvalue distribution_fullexport(struct distribution *distribution,const char *confdir,const char *distdir, struct database *database) {
	return export(distribution, confdir, distdir, database, false);
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

retvalue distribution_exportandfreelist(enum exportwhen when,
		struct distribution *distributions,
		const char *confdir,const char *distdir,
		struct database *database) {
	retvalue result,r;
	bool todo = false;
	struct distribution *d;

	if( when == EXPORT_NEVER ) {
		if( verbose > 10 )
			fprintf(stderr, "Not exporting anything as --export=never specified\n");
		return distribution_freelist(distributions);
	}

	for( d=distributions; d != NULL; d = d->next ) {
		if( d->lookedat && (RET_IS_OK(d->status) ||
			( d->status == RET_NOTHING && when != EXPORT_CHANGED) ||
			when == EXPORT_FORCE)) {
			todo = true;
		}
	}

	if( verbose >= 0 && todo )
		printf("Exporting indices...\n");

	result = RET_NOTHING;
	while( distributions != NULL ) {
		d = distributions;
		distributions = d->next;

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
					r = export(d, confdir, distdir,
							database, true);
					RET_UPDATE(result,r);
					break;
				}
			}
		} else {
			assert( RET_IS_OK(d->status) ||
					( d->status == RET_NOTHING &&
					  when != EXPORT_CHANGED) ||
					when == EXPORT_FORCE);
			r = export(d, confdir, distdir, database, true);
			RET_UPDATE(result,r);
		}

		r = distribution_free(d);
		RET_ENDUPDATE(result,r);
	}
	return result;
}

retvalue distribution_export(enum exportwhen when, struct distribution *distribution,const char *confdir,const char *distdir,struct database *database) {
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
				return export(distribution, confdir, distdir,
						database, true);
				break;
			}
		}

		return RET_NOTHING;
	}
	if( verbose >= 0 )
		printf("Exporting indices...\n");
	return export(distribution, confdir, distdir, database, true);
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

retvalue distribution_loadalloverrides(struct distribution *distribution, const char *overridedir) {
	retvalue r;

	if( distribution->overrides.deb == NULL ) {
		r = override_read(overridedir,distribution->deb_override,&distribution->overrides.deb);
		if( RET_WAS_ERROR(r) ) {
			distribution->overrides.deb = NULL;
			return r;
		}
	}
	if( distribution->overrides.udeb == NULL ) {
		r = override_read(overridedir,distribution->udeb_override,&distribution->overrides.udeb);
		if( RET_WAS_ERROR(r) ) {
			distribution->overrides.udeb = NULL;
			return r;
		}
	}
	if( distribution->overrides.dsc == NULL ) {
		r = override_read(overridedir,distribution->dsc_override,&distribution->overrides.dsc);
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
