/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007 Bernhard R. Link
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
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include "error.h"
#include "mprintf.h"
#include "chunks.h"
#include "dirs.h"
#include "names.h"
#include "release.h"
#include "distribution.h"
#include "filelist.h"
#include "files.h"
#include "ignore.h"
#include "configparser.h"

/* options are zerroed when called, when error is returned contentsopions_done
 * is called by the caller */
retvalue contentsoptions_parse(struct distribution *distribution, struct configiterator *iter) {
	enum contentsflags {
		cf_disable, cf_dummy, cf_udebs, cf_nodebs,
		cf_uncompressed, cf_gz, cf_bz2, cf_COUNT
	};
	bool flags[cf_COUNT];
	static const struct constant contentsflags[] = {
		{"0", cf_disable},
		{"1", cf_dummy},
		{"2", cf_dummy},
		{"udebs", cf_udebs},
		{"nodebs", cf_nodebs},
		{".bz2", cf_bz2},
		{".gz", cf_gz},
		{".", cf_uncompressed},
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
	distribution->contents.compressions = 0;
	if (flags[cf_uncompressed])
		distribution->contents.compressions |= IC_FLAG(ic_uncompressed);
	if (flags[cf_gz])
		distribution->contents.compressions |= IC_FLAG(ic_gzip);
#ifdef HAVE_LIBBZ2
	if (flags[cf_bz2])
		distribution->contents.compressions |= IC_FLAG(ic_bzip2);
#endif
	distribution->contents.flags.udebs = flags[cf_udebs];
	distribution->contents.flags.nodebs = flags[cf_nodebs];

	return RET_OK;
}

static retvalue addpackagetocontents(UNUSED(struct distribution *di), UNUSED(struct target *ta), const char *packagename, const char *chunk, void *data) {
	struct filelist_list *contents = data;
	retvalue r;
	char *section, *filekey;

	r = chunk_getvalue(chunk, "Section", &section);
	/* Ignoring packages without section, as they should not exist anyway */
	if (!RET_IS_OK(r))
		return r;
	r = chunk_getvalue(chunk, "Filename", &filekey);
	/* dito with filekey */
	if (!RET_IS_OK(r)) {
		free(section);
		return r;
	}
	r = filelist_addpackage(contents, packagename, section, filekey);

	free(filekey);
	free(section);
	return r;
}

static retvalue genarchcontents(struct distribution *distribution, architecture_t architecture, packagetype_t type, struct release *release, bool onlyneeded) {
	retvalue r;
	char *contentsfilename;
	struct filetorelease *file;
	struct filelist_list *contents;
	const struct atomlist *components;

	if (type == pt_udeb) {
		if (distribution->contents_components_set)
			components = &distribution->contents_ucomponents;
		else
			components = &distribution->udebcomponents;
	} else {
		if (distribution->contents_components_set)
			components = &distribution->contents_components;
		else
			components = &distribution->components;
	}

	if (components->count == 0)
		return RET_NOTHING;

	if (onlyneeded) {
		struct target *target;
		for (target=distribution->targets; target!=NULL;
				target=target->next) {
			if (target->saved_wasmodified
				&& target->architecture == architecture
				&& target->packagetype == type
				&& atomlist_in(components, target->component))
				break;
		}
		if (target != NULL)
			onlyneeded = false;
	}

	contentsfilename = mprintf((type == pt_udeb)?"uContents-%s":"Contents-%s",
			atoms_architectures[architecture]);
	if (FAILEDTOALLOC(contentsfilename))
		return RET_ERROR_OOM;
	r = release_startfile(release, contentsfilename,
			distribution->contents.compressions,
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
	r = distribution_foreach_package_c(distribution,
			components, architecture, type,
			addpackagetocontents, contents);
	if (!RET_WAS_ERROR(r))
		r = filelist_write(contents, file);
	if (RET_WAS_ERROR(r))
		release_abortfile(file);
	else
		r = release_finishfile(release, file);
	filelist_free(contents);
	return r;
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
	}
	return result;
}
