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
#include <limits.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "error.h"
#include "strlist.h"
#include "mprintf.h"
#include "dirs.h"
#include "names.h"
#include "release.h"
#include "distribution.h"
#include "filelist.h"
#include "files.h"
#include "ignore.h"
#include "configparser.h"
#include "package.h"

/* options are zerroed when called, when error is returned contentsopions_done
 * is called by the caller */
retvalue contentsoptions_parse(struct distribution *distribution, struct configiterator *iter) {
	enum contentsflags {
		cf_disable, cf_dummy, cf_udebs, cf_nodebs,
		cf_uncompressed, cf_gz, cf_bz2, cf_xz,
		cf_percomponent, cf_allcomponents,
		cf_compatsymlink, cf_nocompatsymlink,
		cf_ddebs,
		cf_COUNT
	};
	bool flags[cf_COUNT];
	static const struct constant contentsflags[] = {
		{"0", cf_disable},
		{"1", cf_dummy},
		{"2", cf_dummy},
		{"udebs", cf_udebs},
		{"nodebs", cf_nodebs},
		{"percomponent", cf_percomponent},
		{"allcomponents", cf_allcomponents},
		{"compatsymlink", cf_compatsymlink},
		{"nocompatsymlink", cf_nocompatsymlink},
		{".xz", cf_xz},
		{".bz2", cf_bz2},
		{".gz", cf_gz},
		{".", cf_uncompressed},
		{"ddebs", cf_ddebs},
		{NULL, -1}
	};
	retvalue r;

	distribution->contents.flags.enabled = true;

	memset(flags, 0, sizeof(flags));
	r = config_getflags(iter, "Contents", contentsflags, flags,
			IGNORABLE(unknownfield), "");
	if (r == RET_ERROR_UNKNOWNFIELD)
		(void)fputs(
"Note that the format of the Contents field has changed with reprepro 3.0.0.\n"
"There is no longer a number needed (nor possible) there.\n", stderr);
	if (RET_WAS_ERROR(r))
		return r;
	if (flags[cf_dummy]) {
		(void)fputs(
"Warning: Contents headers in conf/distribution no longer need an\n"
"rate argument. Ignoring the number there, this might cause a error\n"
"future versions.\n", stderr);
	} else if (flags[cf_disable]) {
		(void)fputs(
"Warning: Contents headers in conf/distribution no longer need an\n"
"rate argument. Treating the '0' as sign to not activate Contents-\n"
"-generation, but it will cause an error in future version.\n", stderr);
		distribution->contents.flags.enabled = false;
	}
	if (flags[cf_allcomponents] && flags[cf_compatsymlink]) {
		fprintf(stderr, "Cannot have allcomponents and compatsymlink in the same Contents line!\n");
		return RET_ERROR;
	}
	if (flags[cf_allcomponents] && flags[cf_nocompatsymlink]) {
		fprintf(stderr, "Cannot have allcomponents and nocompatsymlink in the same Contents line!\n");
		return RET_ERROR;
	}

#ifndef HAVE_LIBBZ2
	if (flags[cf_bz2]) {
		fprintf(stderr,
"Warning: Ignoring request to generate .bz2'ed Contents files.\n"
"(bzip2 support disabled at build time.)\n"
"Request was in %s in the Contents header ending in line %u\n",
			config_filename(iter), config_line(iter));
		flags[cf_bz2] = false;
	}
#endif
#ifndef HAVE_LIBLZMA
	if (flags[cf_xz]) {
		fprintf(stderr,
"Warning: Ignoring request to generate .xz'ed Contents files.\n"
"(xz support disabled at build time.)\n"
"Request was in %s in the Contents header ending in line %u\n",
			config_filename(iter), config_line(iter));
		flags[cf_xz] = false;
	}
#endif
	distribution->contents.compressions = 0;
	if (flags[cf_uncompressed])
		distribution->contents.compressions |= IC_FLAG(ic_uncompressed);
	if (flags[cf_gz])
		distribution->contents.compressions |= IC_FLAG(ic_gzip);
#ifdef HAVE_LIBBZ2
	if (flags[cf_bz2])
		distribution->contents.compressions |= IC_FLAG(ic_bzip2);
#endif
#ifdef HAVE_LIBLZMA
	if (flags[cf_xz])
		distribution->contents.compressions |= IC_FLAG(ic_xz);
#endif
	distribution->contents.flags.udebs = flags[cf_udebs];
	distribution->contents.flags.ddebs = flags[cf_ddebs];
	distribution->contents.flags.nodebs = flags[cf_nodebs];
	if (flags[cf_allcomponents])
		distribution->contents.flags.allcomponents = true;
	else
		/* default is now off */
		distribution->contents.flags.allcomponents = false;
	if (flags[cf_percomponent])
		distribution->contents.flags.percomponent = true;
	else if (flags[cf_allcomponents])
		/* if allcomponents is specified, default is off */
		distribution->contents.flags.percomponent = false;
	else
		/* otherwise default is on */
		distribution->contents.flags.percomponent = true;
	/* compat symlink is only possible if there are no files
	 * created there, and on by default unless explicitly specified */
	if (distribution->contents.flags.allcomponents)
		distribution->contents.flags.compatsymlink = false;
	else if (flags[cf_compatsymlink])
		distribution->contents.flags.compatsymlink = true;
	else if (flags[cf_nocompatsymlink])
		distribution->contents.flags.compatsymlink = false;
	else {
		assert(distribution->contents.flags.percomponent);
		distribution->contents.flags.compatsymlink = true;
	}
	assert(distribution->contents.flags.percomponent ||
		distribution->contents.flags.allcomponents);
	return RET_OK;
}

static retvalue addpackagetocontents(struct package *package, void *data) {
	struct filelist_list *contents = data;

	return filelist_addpackage(contents, package);
}

static retvalue gentargetcontents(struct target *target, struct release *release, bool onlyneeded, bool symlink) {
	retvalue result, r;
	char *contentsfilename;
	struct filetorelease *file;
	struct filelist_list *contents;
	struct package_cursor iterator;
	const char *suffix;
	const char *symlink_prefix;

	if (onlyneeded && target->saved_wasmodified)
		onlyneeded = false;

	switch (target->packagetype) {
		case pt_ddeb:
			symlink_prefix = "d";
			suffix = "-ddeb";
			break;
		case pt_udeb:
			symlink_prefix = "s";
			suffix = "-udeb";
			break;
		default:
			symlink_prefix = "";
			suffix = "";
	}

	contentsfilename = mprintf("%s/Contents%s-%s",
			atoms_components[target->component],
			suffix,
			atoms_architectures[target->architecture]);
	if (FAILEDTOALLOC(contentsfilename))
		return RET_ERROR_OOM;

	if (symlink) {
		char *symlinkas = mprintf("%sContents-%s",
				symlink_prefix,
				atoms_architectures[target->architecture]);
		if (FAILEDTOALLOC(symlinkas)) {
			free(contentsfilename);
			return RET_ERROR_OOM;
		}
		r = release_startlinkedfile(release, contentsfilename,
				symlinkas,
				target->distribution->contents.compressions,
				onlyneeded, &file);
		free(symlinkas);
	} else
		r = release_startfile(release, contentsfilename,
				target->distribution->contents.compressions,
				onlyneeded, &file);
	if (!RET_IS_OK(r)) {
		free(contentsfilename);
		return r;
	}
	if (verbose > 0) {
		printf(" generating %s...\n", contentsfilename);
	}
	free(contentsfilename);

	r = filelist_init(&contents);
	if (RET_WAS_ERROR(r)) {
		release_abortfile(file);
		return r;
	}
	result = package_openiterator(target, READONLY, true, &iterator);
	if (RET_IS_OK(result)) {
		while (package_next(&iterator)) {
			r = addpackagetocontents(&iterator.current, contents);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				break;
		}
		r = package_closeiterator(&iterator);
		RET_ENDUPDATE(result, r);
	}
	if (!RET_WAS_ERROR(result))
		result = filelist_write(contents, file);
	if (RET_WAS_ERROR(result))
		release_abortfile(file);
	else
		result = release_finishfile(release, file);
	filelist_free(contents);
	return result;
}

static retvalue genarchcontents(struct distribution *distribution, architecture_t architecture, packagetype_t type, struct release *release, bool onlyneeded) {
	retvalue result = RET_NOTHING, r;
	char *contentsfilename;
	struct filetorelease *file;
	struct filelist_list *contents;
	const struct atomlist *components;
	struct target *target;
	bool combinedonlyifneeded;
	const char *prefix;
	const char *symlink_prefix;

	if (type == pt_ddeb) {
		if (distribution->contents_components_set)
			components = &distribution->contents_dcomponents;
		else
			components = &distribution->ddebcomponents;
		prefix = "d";
		symlink_prefix = "d";
	} else if (type == pt_udeb) {
		if (distribution->contents_components_set)
			components = &distribution->contents_ucomponents;
		else
			components = &distribution->udebcomponents;
		prefix = "u";
		symlink_prefix = "s";
	} else {
		if (distribution->contents_components_set)
			components = &distribution->contents_components;
		else
			components = &distribution->components;
		prefix = "";
		symlink_prefix = "";
	}

	if (components->count == 0)
		return RET_NOTHING;

	combinedonlyifneeded = onlyneeded;

	for (target=distribution->targets; target!=NULL; target=target->next) {
		if (target->architecture != architecture
				|| target->packagetype != type
				|| !atomlist_in(components, target->component))
			continue;
		if (onlyneeded && target->saved_wasmodified)
			combinedonlyifneeded = false;
		if (distribution->contents.flags.percomponent) {
			r = gentargetcontents(target, release, onlyneeded,
					distribution->contents.
					 flags.compatsymlink &&
					!distribution->contents.
					 flags.allcomponents &&
					target->component
					 == components->atoms[0]);
			RET_UPDATE(result, r);
			if (RET_WAS_ERROR(r))
				return r;
		}
	}

	if (!distribution->contents.flags.allcomponents) {
		if (!distribution->contents.flags.compatsymlink) {
			char *symlinkas = mprintf("%sContents-%s",
					symlink_prefix,
					atoms_architectures[architecture]);
			if (FAILEDTOALLOC(symlinkas))
				return RET_ERROR_OOM;
			release_warnoldfileorlink(release, symlinkas,
				distribution->contents.compressions);
			free(symlinkas);
		}
		return RET_OK;
	}

	contentsfilename = mprintf("%sContents-%s",
			prefix,
			atoms_architectures[architecture]);
	if (FAILEDTOALLOC(contentsfilename))
		return RET_ERROR_OOM;
	r = release_startfile(release, contentsfilename,
			distribution->contents.compressions,
			combinedonlyifneeded, &file);
	if (!RET_IS_OK(r)) {
		free(contentsfilename);
		return r;
	}
	if (verbose > 0) {
		printf(" generating %s...\n", contentsfilename);
	}
	free(contentsfilename);

	r = filelist_init(&contents);
	if (RET_WAS_ERROR(r)) {
		release_abortfile(file);
		return r;
	}
	r = package_foreach_c(distribution,
			components, architecture, type,
			addpackagetocontents, contents);
	if (!RET_WAS_ERROR(r))
		r = filelist_write(contents, file);
	if (RET_WAS_ERROR(r))
		release_abortfile(file);
	else
		r = release_finishfile(release, file);
	filelist_free(contents);
	RET_UPDATE(result, r);
	return result;
}

retvalue contents_generate(struct distribution *distribution, struct release *release, bool onlyneeded) {
	retvalue result, r;
	int i;
	const struct atomlist *architectures;

	if (distribution->contents.compressions == 0)
		distribution->contents.compressions = IC_FLAG(ic_gzip);

	result = RET_NOTHING;
	if (distribution->contents_architectures_set) {
		architectures = &distribution->contents_architectures;
	} else {
		architectures = &distribution->architectures;
	}
	for (i = 0 ; i < architectures->count ; i++) {
		architecture_t architecture = architectures->atoms[i];

		if (architecture == architecture_source)
			continue;

		if (!distribution->contents.flags.nodebs) {
			r = genarchcontents(distribution,
					architecture, pt_deb,
					release, onlyneeded);
			RET_UPDATE(result, r);
		}
		if (distribution->contents.flags.udebs) {
			r = genarchcontents(distribution,
					architecture, pt_udeb,
					release, onlyneeded);
			RET_UPDATE(result, r);
		}
		if (distribution->contents.flags.ddebs) {
			r = genarchcontents(distribution,
					architecture, pt_ddeb,
					release, onlyneeded);
			RET_UPDATE(result, r);
		}
	}
	return result;
}
