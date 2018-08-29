/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2008,2009,2010,2016 Bernhard R. Link
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
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "error.h"
#include "mprintf.h"
#include "atoms.h"
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
#include "byhandhook.h"
#include "package.h"
#include "distribution.h"

static retvalue distribution_free(struct distribution *distribution) {
	retvalue result, r;
	bool needsretrack = false;

	if (distribution != NULL) {
		free(distribution->suite);
		free(distribution->fakecomponentprefix);
		free(distribution->version);
		free(distribution->origin);
		free(distribution->notautomatic);
		free(distribution->butautomaticupgrades);
		free(distribution->label);
		free(distribution->description);
		free(distribution->signed_by);
		free(distribution->deb_override);
		free(distribution->udeb_override);
		free(distribution->dsc_override);
		free(distribution->uploaders);
		atomlist_done(&distribution->udebcomponents);
		atomlist_done(&distribution->architectures);
		atomlist_done(&distribution->components);
		strlist_done(&distribution->signwith);
		strlist_done(&distribution->updates);
		strlist_done(&distribution->pulls);
		strlist_done(&distribution->alsoaccept);
		exportmode_done(&distribution->dsc);
		exportmode_done(&distribution->deb);
		exportmode_done(&distribution->udeb);
		atomlist_done(&distribution->contents_architectures);
		atomlist_done(&distribution->contents_components);
		atomlist_done(&distribution->contents_ucomponents);
		override_free(distribution->overrides.deb);
		override_free(distribution->overrides.udeb);
		override_free(distribution->overrides.dsc);
		logger_free(distribution->logger);
		if (distribution->uploaderslist != NULL) {
			uploaders_unlock(distribution->uploaderslist);
		}
		byhandhooks_free(distribution->byhandhooks);
		result = RET_OK;

		while (distribution->targets != NULL) {
			struct target *next = distribution->targets->next;

			if (distribution->targets->staletracking)
				needsretrack = true;

			r = target_free(distribution->targets);
			RET_UPDATE(result, r);
			distribution->targets = next;
		}
		if (distribution->tracking != dt_NONE && needsretrack) {
			fprintf(stderr,
"WARNING: Tracking data of '%s' might have become out of date.\n"
"Consider running retrack to avoid getting funny effects.\n",
					distribution->codename);
		}
		free(distribution->codename);
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
	int i, j;
	struct target *t;
	struct target *last = NULL;
	bool has_source = false;

	for (i = 0 ; i < distribution->components.count ; i++) {
		component_t c = distribution->components.atoms[i];
		for (j = 0 ; j < distribution->architectures.count ; j++) {
			architecture_t a = distribution->architectures.atoms[j];

			if (a == architecture_source) {
				has_source = true;
				continue;
			}
			if (a == architecture_all) {
				fprintf(stderr,
"Error: Distribution %s contains an architecture called 'all'.\n",
						distribution->codename);
				return RET_ERROR;
			}
			if (strcmp(atoms_architectures[a], "any") == 0) {
				fprintf(stderr,
"Error: Distribution %s contains an architecture called 'any'.\n",
						distribution->codename);
				return RET_ERROR;
			}

			r = target_initialize_binary(
					distribution,
					c, a,
					&distribution->deb,
					distribution->readonly,
					distribution->exportoptions[deo_noexport],
					distribution->fakecomponentprefix,
					&t);
			if (RET_IS_OK(r)) {
				if (last != NULL) {
					last->next = t;
				} else {
					distribution->targets = t;
				}
				last = t;
			}
			if (RET_WAS_ERROR(r))
				return r;
			if (atomlist_in(&distribution->udebcomponents, c)) {
				r = target_initialize_ubinary(
						distribution,
						c, a,
						&distribution->udeb,
						distribution->readonly,
						distribution->exportoptions
							[deo_noexport],
						distribution->fakecomponentprefix,
						&t);
				if (RET_IS_OK(r)) {
					if (last != NULL) {
						last->next = t;
					} else {
						distribution->targets = t;
					}
					last = t;
				}
				if (RET_WAS_ERROR(r))
					return r;

			}
		}
		/* check if this distribution contains source
		 * (yes, yes, source is not really an architecture, but
		 *  the .changes files started with this...) */
		if (has_source) {
			r = target_initialize_source(distribution,
					c, &distribution->dsc,
					distribution->readonly,
					distribution->exportoptions
							[deo_noexport],
					distribution->fakecomponentprefix, &t);
			if (last != NULL) {
				last->next = t;
			} else {
				distribution->targets = t;
			}
			last = t;
			if (RET_WAS_ERROR(r))
				return r;
		}
	}
	return RET_OK;
}

struct read_distribution_data {
	struct distribution *distributions;
};

CFstartparse(distribution) {
	CFstartparseVAR(distribution, result_p);
	struct distribution *n;
	retvalue r;

	n = zNEW(struct distribution);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	/* set some default value: */
	r = exportmode_init(&n->udeb, true, NULL, "Packages");
	if (RET_WAS_ERROR(r)) {
		(void)distribution_free(n);
		return r;
	}
	r = exportmode_init(&n->deb, true, "Release", "Packages");
	if (RET_WAS_ERROR(r)) {
		(void)distribution_free(n);
		return r;
	}
	r = exportmode_init(&n->dsc, false, "Release", "Sources");
	if (RET_WAS_ERROR(r)) {
		(void)distribution_free(n);
		return r;
	}
	*result_p = n;
	return RET_OK;
}

static bool notpropersuperset(const struct atomlist *allowed, const char *allowedname, const struct atomlist *check, const char *checkname, const char **atoms, const struct distribution *d) {
	atom_t missing;

	if (!atomlist_subset(allowed, check, &missing)) {
		fprintf(stderr,
"In distribution description of '%s' (line %u to %u in %s):\n"
"%s contains '%s' not found in %s!\n",
				d->codename,
				d->firstline, d->lastline, d->filename,
				checkname, atoms[missing], allowedname);
		return true;
	}
	return false;
}

static inline retvalue checkcomponentsequalduetofake(const struct distribution *d) {
	size_t l;
	int i, j;

	if (d->fakecomponentprefix == NULL)
		return RET_OK;

	l = strlen(d->fakecomponentprefix);

	for (i = 0 ; i < d->components.count ; i++) {
		const char *c1 = atoms_components[d->components.atoms[i]];

		if (strncmp(c1, d->fakecomponentprefix, l) != 0)
			continue;
		if (d->fakecomponentprefix[l] != '/')
			continue;

		for (j = 0 ; i < d->components.count ; j++) {
			const char *c2;

			if (j == i)
				continue;

			c2 = atoms_components[d->components.atoms[j]];

			if (strcmp(c1 + l + 1, c2) == 0) {
				fprintf(stderr,
"ERROR: distribution '%s' has components '%s' and '%s',\n"
"which would be output to the same place due to FakeComponentPrefix '%s'.\n",
					d->codename, c1, c2,
					d->fakecomponentprefix);
				return RET_ERROR;
			}
		}
	}
	return RET_OK;
}

CFfinishparse(distribution) {
	CFfinishparseVARS(distribution, n, last_p, mydata);
	struct distribution *d;
	retvalue r;

	if (!complete) {
		distribution_free(n);
		return RET_NOTHING;
	}
	n->filename = config_filename(iter);
	n->firstline = config_firstline(iter);
	n->lastline = config_line(iter) - 1;

	/* Do some consitency checks */
	for (d = mydata->distributions; d != NULL; d = d->next) {
		if (strcmp(d->codename, n->codename) == 0) {
			fprintf(stderr,
"Multiple distributions with the common codename: '%s'!\n"
"First was in %s line %u to %u,\n"
"now another in lines %u to %u of %s.\n",
				n->codename, d->filename,
				d->firstline, d->lastline,
				n->firstline, n->lastline,
				n->filename);
			distribution_free(n);
			return RET_ERROR;
		}
	}

	if (notpropersuperset(&n->architectures, "Architectures",
			    &n->contents_architectures, "ContentsArchitectures",
			    atoms_architectures, n) ||
	    notpropersuperset(&n->components, "Components",
			    &n->contents_components, "ContentsComponents",
			    atoms_components, n) ||
	    notpropersuperset(&n->udebcomponents, "UDebComponents",
			    &n->contents_ucomponents, "ContentsUComponents",
			    atoms_components, n) ||
	    // TODO: instead of checking here make sure it can have more
	    // in the rest of the code...:
	    notpropersuperset(&n->components, "Components",
			    &n->udebcomponents, "UDebComponents",
			    atoms_components, n)) {
		(void)distribution_free(n);
		return RET_ERROR;
	}
	/* overwrite creation of contents files based on given lists: */
	if (n->contents_components_set) {
		if (n->contents_components.count > 0) {
			n->contents.flags.enabled = true;
			n->contents.flags.nodebs = false;
		} else {
			n->contents.flags.nodebs = true;
		}
	}
	if (n->contents_ucomponents_set) {
		if (n->contents_ucomponents.count > 0) {
			n->contents.flags.enabled = true;
			n->contents.flags.udebs = true;
		} else {
			n->contents.flags.udebs = false;
		}
	}
	if (n->contents_architectures_set) {
		if (n->contents_architectures.count > 0)
			n->contents.flags.enabled = true;
		else
			n->contents.flags.enabled = false;
	}

	r = checkcomponentsequalduetofake(n);
	if (RET_WAS_ERROR(r)) {
		(void)distribution_free(n);
		return r;
	}

	/* prepare substructures */

	r = createtargets(n);
	if (RET_WAS_ERROR(r)) {
		(void)distribution_free(n);
		return r;
	}
	n->status = RET_NOTHING;
	n->lookedat = false;
	n->selected = false;

	/* put in linked list */
	if (*last_p == NULL)
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
CFallSETPROC(distribution, butautomaticupgrades)
CFtruthSETPROC2(distribution, readonly, readonly)
CFallSETPROC(distribution, label)
CFallSETPROC(distribution, description)
CFallSETPROC(distribution, signed_by)
CFsignwithSETPROC(distribution, signwith)
CFfileSETPROC(distribution, deb_override)
CFfileSETPROC(distribution, udeb_override)
CFfileSETPROC(distribution, dsc_override)
CFfileSETPROC(distribution, uploaders)
CFuniqstrlistSETPROC(distribution, alsoaccept)
CFstrlistSETPROC(distribution, updates)
CFstrlistSETPROC(distribution, pulls)
CFinternatomsSETPROC(distribution, components, checkforcomponent, at_component)
CFinternatomsSETPROC(distribution, architectures, checkforarchitecture, at_architecture)
CFatomsublistSETPROC(distribution, contents_architectures, at_architecture, architectures, "Architectures")
CFatomsublistSETPROC(distribution, contents_components, at_component, components, "Components")
CFatomsublistSETPROC(distribution, udebcomponents, at_component, components, "Components")
CFatomsublistSETPROC(distribution, contents_ucomponents, at_component, udebcomponents, "UDebComponents")
CFexportmodeSETPROC(distribution, udeb)
CFexportmodeSETPROC(distribution, deb)
CFexportmodeSETPROC(distribution, dsc)
CFcheckvalueSETPROC(distribution, codename, checkforcodename)
CFcheckvalueSETPROC(distribution, fakecomponentprefix, checkfordirectoryandidentifier)
CFtimespanSETPROC(distribution, validfor)

CFUSETPROC(distribution, Contents) {
	CFSETPROCVAR(distribution, d);
	return contentsoptions_parse(d, iter);
}
CFUSETPROC(distribution, logger) {
	CFSETPROCVAR(distribution, d);
	return logger_init(iter, &d->logger);
}
CFUSETPROC(distribution, Tracking) {
	CFSETPROCVAR(distribution, d);
	return tracking_parse(d, iter);
}

CFUSETPROC(distribution, byhandhooks) {
	CFSETPROCVAR(distribution, d);

	return byhandhooks_parse(iter, &d->byhandhooks);
}

static const struct constant exportnames[deo_COUNT+1] = {
	{"noexport", deo_noexport},
	{"keepunknown", deo_keepunknown},
	{NULL, 0}
};

CFUSETPROC(distribution, exportoptions) {
	CFSETPROCVAR(distribution, d);
	return config_getflags(iter, name, exportnames, d->exportoptions,
			IGNORABLE(unknownfield),
			"(allowed values: noexport, keepunknown)");
}

static const struct configfield distributionconfigfields[] = {
	CF("AlsoAcceptFor",	distribution,	alsoaccept),
	CFr("Architectures",	distribution,	architectures),
	CF("ByHandHooks",	distribution,	byhandhooks),
	CFr("Codename",		distribution,	codename),
	CFr("Components",	distribution,	components),
	CF("ContentsArchitectures", distribution, contents_architectures),
	CF("ContentsComponents", distribution,	contents_components),
	CF("Contents",		distribution,	Contents),
	CF("ContentsUComponents", distribution,	contents_ucomponents),
	CF("DebIndices",	distribution,	deb),
	CF("DebOverride",	distribution,	deb_override),
	CF("Description",	distribution,	description),
	CF("Signed-By",		distribution,	signed_by),
	CF("DscIndices",	distribution,	dsc),
	CF("DscOverride",	distribution,	dsc_override),
	CF("FakeComponentPrefix", distribution,	fakecomponentprefix),
	CF("Label",		distribution,	label),
	CF("Log",		distribution,	logger),
	CF("NotAutomatic",	distribution,	notautomatic),
	CF("ButAutomaticUpgrades", distribution, butautomaticupgrades),
	CF("Origin",		distribution,	origin),
	CF("Pull",		distribution,	pulls),
	CF("ReadOnly",		distribution,	readonly),
	CF("ExportOptions",	distribution,	exportoptions),
	CF("SignWith",		distribution,	signwith),
	CF("Suite",		distribution,	suite),
	CF("Tracking",		distribution,	Tracking),
	CF("UDebComponents",	distribution,	udebcomponents),
	CF("UDebIndices",	distribution,	udeb),
	CF("UDebOverride",	distribution,	udeb_override),
	CF("Update",		distribution,	updates),
	CF("Uploaders",		distribution,	uploaders),
	CF("ValidFor",		distribution,	validfor),
	CF("Version",		distribution,	version)
};

/* read specification of all distributions */
retvalue distribution_readall(struct distribution **distributions) {
	struct read_distribution_data mydata;
	retvalue result;

	mydata.distributions = NULL;

	// TODO: readd some way to tell about -b or --confdir here?
	/*
	result = regularfileexists(fn);
	if (RET_WAS_ERROR(result)) {
		fprintf(stderr, "Could not find '%s'!\n"
"(Have you forgotten to specify a basedir by -b?\n"
"To only set the conf/ dir use --confdir)\n", fn);
		free(mydata.filter.found);
		free(fn);
		return RET_ERROR_MISSING;
	}
	*/

	result = configfile_parse("distributions",
			IGNORABLE(unknownfield),
			startparsedistribution, finishparsedistribution,
			"distribution definition",
			distributionconfigfields,
			ARRAYCOUNT(distributionconfigfields),
			&mydata);
	if (result == RET_ERROR_UNKNOWNFIELD)
		fprintf(stderr,
"Use --ignore=unknownfield to ignore unknown fields\n");
	if (RET_WAS_ERROR(result)) {
		distribution_freelist(mydata.distributions);
		return result;
	}
	if (mydata.distributions == NULL) {
		fprintf(stderr,
"No distribution definitions found in %s/distributions!\n",
				global.confdir);
		distribution_freelist(mydata.distributions);
		return RET_ERROR_MISSING;
	}
	*distributions = mydata.distributions;
	return RET_OK;
}

/* call <action> for each package */
retvalue package_foreach(struct distribution *distribution, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, action_each_package action, action_each_target target_action, void *data) {
	retvalue result, r;
	struct target *t;
	struct package_cursor iterator;

	result = RET_NOTHING;
	for (t = distribution->targets ; t != NULL ; t = t->next) {
		if (!target_matches(t, components, architectures, packagetypes))
			continue;
		if (target_action != NULL) {
			r = target_action(t, data);
			if (RET_WAS_ERROR(r))
				return result;
			if (r == RET_NOTHING)
				continue;
		}
		r = package_openiterator(t, READONLY, true, &iterator);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			return result;
		while (package_next(&iterator)) {
			r = action(&iterator.current, data);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
		r = package_closeiterator(&iterator);
		RET_ENDUPDATE(result, r);
		if (RET_WAS_ERROR(result))
			return result;
	}
	return result;
}

retvalue package_foreach_c(struct distribution *distribution, const struct atomlist *components, architecture_t architecture, packagetype_t packagetype, action_each_package action, void *data) {
	retvalue result, r;
	struct target *t;
	struct package_cursor iterator;

	result = RET_NOTHING;
	for (t = distribution->targets ; t != NULL ; t = t->next) {
		if (components != NULL &&
				!atomlist_in(components, t->component))
			continue;
		if (limitation_missed(architecture, t->architecture))
			continue;
		if (limitation_missed(packagetype, t->packagetype))
			continue;
		r = package_openiterator(t, READONLY, true, &iterator);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			return result;
		while (package_next(&iterator)) {
			r = action(&iterator.current, data);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
		r = package_closeiterator(&iterator);
		RET_ENDUPDATE(result, r);
		if (RET_WAS_ERROR(result))
			return result;
	}
	return result;
}

struct target *distribution_gettarget(const struct distribution *distribution, component_t component, architecture_t architecture, packagetype_t packagetype) {
	struct target *t = distribution->targets;

	assert (atom_defined(component));
	assert (atom_defined(architecture));
	assert (atom_defined(packagetype));

	// TODO: think about making read only access and only alowing readwrite when lookedat is set

	while (t != NULL &&
			(t->component != component ||
			  t->architecture != architecture ||
			  t->packagetype != packagetype)) {
		t = t->next;
	}
	return t;
}

struct target *distribution_getpart(const struct distribution *distribution, component_t component, architecture_t architecture, packagetype_t packagetype) {
	struct target *t = distribution->targets;

	assert (atom_defined(component));
	assert (atom_defined(architecture));
	assert (atom_defined(packagetype));

	while (t != NULL &&
			(t->component != component ||
			  t->architecture != architecture ||
			  t->packagetype != packagetype)) {
		t = t->next;
	}
	if (t == NULL) {
		fprintf(stderr,
"Internal error in distribution_getpart: Bogus request for c='%s' a='%s' t='%s' in '%s'!\n",
				atoms_components[component],
				atoms_architectures[architecture],
				atoms_packagetypes[packagetype],
				distribution->codename);
		abort();
	}
	return t;
}

/* mark all distributions matching one of the first argc argv */
retvalue distribution_match(struct distribution *alldistributions, int argc, const char *argv[], bool lookedat, bool allowreadonly) {
	struct distribution *d;
	bool found[argc], unusable_as_suite[argc];
	struct distribution *has_suite[argc];
	int i;

	assert (alldistributions != NULL);

	if (argc <= 0) {
		for (d = alldistributions ; d != NULL ; d = d->next) {
			if (!allowreadonly && d->readonly)
				continue;
			d->selected = true;
			d->lookedat = lookedat;
		}
		return RET_OK;
	}
	memset(found, 0, sizeof(found));
	memset(unusable_as_suite, 0, sizeof(unusable_as_suite));
	memset(has_suite, 0, sizeof(has_suite));

	for (d = alldistributions ; d != NULL ; d = d->next) {
		for (i = 0 ; i < argc ; i++) {
			if (strcmp(argv[i], d->codename) == 0) {
				assert (!found[i]);
				found[i] = true;
				d->selected = true;
				if (lookedat)
					d->lookedat = lookedat;
				if (!allowreadonly && d->readonly) {
					fprintf(stderr,
"Error: %s is readonly, so operation not allowed!\n",
							d->codename);
					return RET_ERROR;
				}
			} else if (d->suite != NULL &&
					strcmp(argv[i], d->suite) == 0) {
				if (has_suite[i] != NULL)
					unusable_as_suite[i] = true;
				has_suite[i] = d;
			}
		}
	}
	for (i = 0 ; i < argc ; i++) {
		if (!found[i]) {
			if (has_suite[i] != NULL && !unusable_as_suite[i]) {
				if (!allowreadonly && has_suite[i]->readonly) {
					fprintf(stderr,
"Error: %s is readonly, so operation not allowed!\n",
							has_suite[i]->codename);
					return RET_ERROR;
				}
				has_suite[i]->selected = true;
				if (lookedat)
					has_suite[i]->lookedat = lookedat;
				continue;
			}
			fprintf(stderr,
"No distribution definition of '%s' found in '%s/distributions'!\n",
				argv[i], global.confdir);
			if (unusable_as_suite[i])
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
	while (d != NULL && strcmp(name, d->codename) != 0)
		d = d->next;
	if (d == NULL) {
		for (d2 = alldistributions; d2 != NULL ; d2 = d2->next) {
			if (d2->suite == NULL)
				continue;
			if (strcmp(name, d2->suite) != 0)
				continue;
			if (d != NULL) {
				fprintf(stderr,
"No distribution has '%s' as codename, but multiple as suite name,\n"
"thus it cannot be used to determine a distribution.\n", name);
				return RET_ERROR_MISSING;
			}
			d = d2;
		}
	}
	if (d == NULL) {
		fprintf(stderr,
"Cannot find definition of distribution '%s'!\n",
				name);
		return RET_ERROR_MISSING;
	}
	d->selected = true;
	if (lookedat)
		d->lookedat = true;
	*distribution = d;
	return RET_OK;
}

retvalue distribution_snapshot(struct distribution *distribution, const char *name) {
	struct target *target;
	retvalue result, r;
	struct release *release;
	char *id;

	assert (distribution != NULL);

	r = release_initsnapshot(distribution->codename, name, &release);
	if (RET_WAS_ERROR(r))
		return r;

	result = RET_NOTHING;
	for (target=distribution->targets; target != NULL ;
	                                   target = target->next) {
		r = release_mkdir(release, target->relativedirectory);
		RET_ENDUPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		r = target_export(target, false, true, release);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		if (target->exportmode->release != NULL) {
			r = release_directorydescription(release, distribution,
					target, target->exportmode->release,
					false);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
	}
	if (!RET_WAS_ERROR(result)) {
		result = release_prepare(release, distribution, false);
		assert (result != RET_NOTHING);
	}
	if (RET_WAS_ERROR(result)) {
		release_free(release);
		return result;
	}
	result = release_finish(release, distribution);
	if (RET_WAS_ERROR(result))
		return r;
	id = mprintf("s=%s=%s", distribution->codename, name);
	if (FAILEDTOALLOC(id))
		return RET_ERROR_OOM;
	r = package_foreach(distribution,
			atom_unknown, atom_unknown, atom_unknown,
			package_referenceforsnapshot, NULL, id);
	free(id);
	RET_UPDATE(result, r);
	return result;
}

static retvalue export(struct distribution *distribution, bool onlyneeded) {
	struct target *target;
	retvalue result, r;
	struct release *release;

	assert (distribution != NULL);

	if (distribution->exportoptions[deo_noexport])
		return RET_NOTHING;

	if (distribution->readonly) {
		fprintf(stderr,
"Error: trying to re-export read-only distribution %s\n",
				distribution->codename);
		return RET_ERROR;
	}

	r = release_init(&release, distribution->codename, distribution->suite,
			distribution->fakecomponentprefix);
	if (RET_WAS_ERROR(r))
		return r;

	result = RET_NOTHING;
	for (target=distribution->targets; target != NULL ;
	                                   target = target->next) {
		r = release_mkdir(release, target->relativedirectory);
		RET_ENDUPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		r = target_export(target, onlyneeded, false, release);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			break;
		if (target->exportmode->release != NULL) {
			r = release_directorydescription(release, distribution,
					target, target->exportmode->release,
					onlyneeded);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
	}
	if (!RET_WAS_ERROR(result) && distribution->contents.flags.enabled) {
		r = contents_generate(distribution, release, onlyneeded);
	}
	if (!RET_WAS_ERROR(result)) {
		result = release_prepare(release, distribution, onlyneeded);
		if (result == RET_NOTHING) {
			release_free(release);
			return result;
		}
	}
	if (RET_WAS_ERROR(result)) {
		bool workleft = false;
		release_free(release);
		fprintf(stderr, "ERROR: Could not finish exporting '%s'!\n",
				distribution->codename);
		for (target=distribution->targets; target != NULL ;
		                                   target = target->next) {
			workleft |= target->saved_wasmodified;
		}
		if (workleft) {
			(void)fputs(
"This means that from outside your repository will still look like before (and\n"
"should still work if this old state worked), but the changes intended with this\n"
"call will not be visible until you call export directly (via reprepro export)\n"
"Changes will also get visible when something else changes the same file and\n"
"thus creates a new export of that file, but even changes to other parts of the\n"
"same distribution will not!\n",
					stderr);
		}
	} else {
		r = release_finish(release, distribution);
		RET_UPDATE(result, r);
	}
	if (RET_IS_OK(result))
		distribution->status = RET_NOTHING;
	return result;
}

retvalue distribution_fullexport(struct distribution *distribution) {
	return export(distribution, false);
}

retvalue distribution_freelist(struct distribution *distributions) {
	retvalue result, r;

	result = RET_NOTHING;
	while (distributions != NULL) {
		struct distribution *d = distributions->next;
		r = distribution_free(distributions);
		RET_UPDATE(result, r);
		distributions = d;
	}
	return result;
}

retvalue distribution_exportlist(enum exportwhen when, struct distribution *distributions) {
	retvalue result, r;
	bool todo = false;
	struct distribution *d;

	if (when == EXPORT_SILENT_NEVER) {
		for (d = distributions ; d != NULL ; d = d->next) {
			struct target *t;

			for (t = d->targets ; t != NULL ; t = t->next)
				t->wasmodified = false;
		}
		return RET_NOTHING;
	}
	if (when == EXPORT_NEVER) {
		if (verbose > 10)
			fprintf(stderr,
"Not exporting anything as --export=never specified\n");
		return RET_NOTHING;
	}

	for (d=distributions; d != NULL; d = d->next) {
		if (d->omitted || !d->selected || d->exportoptions[deo_noexport])
			continue;
		if (d->lookedat && (RET_IS_OK(d->status) ||
			(d->status == RET_NOTHING && when != EXPORT_CHANGED) ||
			when == EXPORT_FORCE)) {
			todo = true;
		}
	}

	if (verbose >= 0 && todo)
		printf("Exporting indices...\n");

	result = RET_NOTHING;
	for (d=distributions; d != NULL; d = d->next) {
		if (d->exportoptions[deo_noexport])
			continue;
		if (d->omitted || !d->selected)
			continue;
		if (!d->lookedat) {
			if (verbose >= 30)
				printf(
" Not exporting %s because not looked at.\n", d->codename);
		} else if ((RET_WAS_ERROR(d->status)||interrupted()) &&
		           when != EXPORT_FORCE) {
			if (verbose >= 10)
				fprintf(stderr,
" Not exporting %s because there have been errors and no --export=force.\n",
						d->codename);
		} else if (d->status==RET_NOTHING && when==EXPORT_CHANGED) {
			struct target *t;

			if (verbose >= 10)
				printf(
" Not exporting %s because of no recorded changes and --export=changed.\n",
						d->codename);

			/* some paranoid check */

			for (t = d->targets ; t != NULL ; t = t->next) {
				if (t->wasmodified) {
					fprintf(stderr,
"A paranoid check found distribution %s would not have been exported,\n"
"despite having parts that are marked changed by deeper code.\n"
"Please report this and how you got this message as bugreport. Thanks.\n"
"Doing a export despite --export=changed....\n",
						d->codename);
					r = export(d, true);
					RET_UPDATE(result, r);
					break;
				}
			}
		} else {
			assert (RET_IS_OK(d->status) ||
					(d->status == RET_NOTHING &&
					  when != EXPORT_CHANGED) ||
					when == EXPORT_FORCE);
			r = export(d, true);
			RET_UPDATE(result, r);
		}
	}
	return result;
}


/* get a pointer to the apropiate part of the linked list */
struct distribution *distribution_find(struct distribution *distributions, const char *name) {
	struct distribution *d = distributions, *r;

	while (d != NULL && strcmp(d->codename, name) != 0)
		d = d->next;
	if (d != NULL)
		return d;
	d = distributions;
	while (d != NULL && !strlist_in(&d->alsoaccept, name))
		d = d->next;
	r = d;
	if (r != NULL) {
		d = d->next;
		while (d != NULL && ! strlist_in(&d->alsoaccept, name))
			d = d->next;
		if (d == NULL)
			return r;
		fprintf(stderr,
"No distribution has codename '%s' and multiple have it in AlsoAcceptFor!\n",
				name);
		return NULL;
	}
	d = distributions;
	while (d != NULL && (d->suite == NULL || strcmp(d->suite, name) != 0))
		d = d->next;
	r = d;
	if (r == NULL) {
		fprintf(stderr, "No distribution named '%s' found!\n", name);
		return NULL;
	}
	d = d->next;
	while (d != NULL && (d->suite == NULL || strcmp(d->suite, name) != 0))
		d = d->next;
	if (d == NULL)
		return r;
	fprintf(stderr,
"No distribution has codename '%s' and multiple have it as suite-name!\n",
			name);
	return NULL;
}

retvalue distribution_loadalloverrides(struct distribution *distribution) {
	retvalue r;

	if (distribution->overrides.deb == NULL) {
		r = override_read(distribution->deb_override,
				&distribution->overrides.deb, false);
		if (RET_WAS_ERROR(r)) {
			distribution->overrides.deb = NULL;
			return r;
		}
	}
	if (distribution->overrides.udeb == NULL) {
		r = override_read(distribution->udeb_override,
				&distribution->overrides.udeb, false);
		if (RET_WAS_ERROR(r)) {
			distribution->overrides.udeb = NULL;
			return r;
		}
	}
	if (distribution->overrides.dsc == NULL) {
		r = override_read(distribution->dsc_override,
				&distribution->overrides.dsc, true);
		if (RET_WAS_ERROR(r)) {
			distribution->overrides.dsc = NULL;
			return r;
		}
	}
	if (distribution->overrides.deb != NULL ||
	    distribution->overrides.udeb != NULL ||
	    distribution->overrides.dsc != NULL)
		return RET_OK;
	else
		return RET_NOTHING;
}

retvalue distribution_loaduploaders(struct distribution *distribution) {
	if (distribution->uploaders != NULL) {
		if (distribution->uploaderslist != NULL)
			return RET_OK;
		return uploaders_get(&distribution->uploaderslist,
				distribution->uploaders);
	} else {
		distribution->uploaderslist = NULL;
		return RET_NOTHING;
	}
}

void distribution_unloaduploaders(struct distribution *distribution) {
	if (distribution->uploaderslist != NULL) {
		uploaders_unlock(distribution->uploaderslist);
		distribution->uploaderslist = NULL;
	}
}

retvalue distribution_prepareforwriting(struct distribution *distribution) {
	retvalue r;

	if (distribution->readonly) {
		fprintf(stderr,
"Error: distribution %s is read-only.\n"
"Current operation not possible because it needs write access.\n",
				distribution->codename);
		return RET_ERROR;
	}

	if (distribution->logger != NULL) {
		r = logger_prepare(distribution->logger);
		if (RET_WAS_ERROR(r))
			return r;
	}
	distribution->lookedat = true;
	return RET_OK;
}

/* delete every package decider returns RET_OK for */
retvalue package_remove_each(struct distribution *distribution, const struct atomlist *components, const struct atomlist *architectures, const struct atomlist *packagetypes, action_each_package decider, struct trackingdata *trackingdata, void *data) {
	retvalue result, r;
	struct target *t;
	struct package_cursor iterator;

	if (distribution->readonly) {
		fprintf(stderr,
"Error: trying to delete packages in read-only distribution %s.\n",
				distribution->codename);
		return RET_ERROR;
	}

	result = RET_NOTHING;
	for (t = distribution->targets ; t != NULL ; t = t->next) {
		if (!target_matches(t, components, architectures, packagetypes))
			continue;
		r = package_openiterator(t, READWRITE, true, &iterator);
		RET_UPDATE(result, r);
		if (RET_WAS_ERROR(r))
			return result;
		while (package_next(&iterator)) {
			r = decider(&iterator.current, data);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
			if (RET_IS_OK(r)) {
				r = package_remove_by_cursor(&iterator,
					distribution->logger, trackingdata);
				RET_UPDATE(result, r);
				RET_UPDATE(distribution->status, r);
			}
		}
		r = package_closeiterator(&iterator);
		RET_ENDUPDATE(result, r);
		if (RET_WAS_ERROR(result))
			return result;
	}
	return result;
}
