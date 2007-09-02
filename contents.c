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
#include "md5sum.h"
#include "dirs.h"
#include "names.h"
#include "release.h"
#include "distribution.h"
#include "filelist.h"
#include "files.h"
#include "ignore.h"
#include "configparser.h"

extern int verbose;

void contentsoptions_done(struct contentsoptions *options) {
	strlist_done(&options->architectures);
	strlist_done(&options->components);
	strlist_done(&options->ucomponents);
}

struct distribution;

/* options are zerroed when called, when error is returned contentsopions_done
 * is called by the caller */
retvalue contentsoptions_parse(struct distribution *distribution, struct configiterator *iter) {
	enum contentsflags { cf_udebs, cf_nodebs, cf_uncompressed, cf_gz, cf_bz2, cf_COUNT};
	bool_t flags[cf_COUNT];
	static const struct constant contentsflags[] = {
		{"udebs", cf_udebs},
		{"nodebs", cf_nodebs},
		{".bz2", cf_bz2},
		{".gz", cf_gz},
		{".", cf_uncompressed},
		{NULL,	-1}
	};
	retvalue r;
	long long l;

	r = config_getnumber(iter, "Contents rate", &l, 0, INT_MAX);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;

	distribution->contents.rate = l;

	memset(flags, 0, sizeof(flags));
	r = config_getflags(iter, "Contents", contentsflags, flags,
			IGNORABLE(unknownfield), "");
	if( RET_WAS_ERROR(r) )
		return r;

#ifndef HAVE_LIBBZ2
	if( flags[cf_bz2] ) {
		fprintf(stderr,
"Warning: Ignoring request to generate .bz2'ed Contents files.\n"
"(not compiled with libbzip2, so no support available.)\n"
"Request was in %s in the Contents-header ending in line %u\n",
			config_filename(iter), config_line(iter));
		flags[cf_bz2] = FALSE;
	}
#endif
	if( !flags[cf_uncompressed] && !flags[cf_gz] && !flags[cf_bz2] )
		flags[cf_gz] = TRUE;
	distribution->contents.compressions = 0;
	if( flags[cf_uncompressed] )
		distribution->contents.compressions |= IC_FLAG(ic_uncompressed);
	if( flags[cf_gz] )
		distribution->contents.compressions |= IC_FLAG(ic_gzip);
#ifdef HAVE_LIBBZ2
	if( flags[cf_bz2] )
		distribution->contents.compressions |= IC_FLAG(ic_bzip2);
#endif
	distribution->contents.flags.udebs = flags[cf_udebs];
	distribution->contents.flags.nodebs = flags[cf_nodebs];

	if( distribution->contents.rate == 0 )
		return RET_NOTHING;
	return RET_OK;
}

struct addcontentsdata {
	int rate;
	size_t work, leisure;
	struct filelist_list *contents;
	struct database *db;
};

static retvalue addpackagetocontents(void *data, const char *packagename, const char *chunk) {
	struct addcontentsdata *d = data;
	const struct filelist_package *package;
	retvalue r;
	char *section, *filekey;

	r = chunk_getvalue(chunk,"Section",&section);
	/* Ignoring packages without section, as they should not exist anyway */
	if( !RET_IS_OK(r) )
		return r;
	r = chunk_getvalue(chunk,"Filename",&filekey);
	/* dito with filekey */
	if( !RET_IS_OK(r) ) {
		free(section);
		return r;
	}
	r = filelist_newpackage(d->contents, packagename, section, &package);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;
	r = files_getfilelist(d->db, filekey, package, d->contents);
	if( r == RET_NOTHING ) {
		if( d->rate <= 1 || d->work <= d->leisure/(d->rate-1) ) {
			if( verbose > 3 )
				printf("Reading filelist for %s\n", filekey);
			r = files_genfilelist(d->db, filekey, package, d->contents);
			if( RET_IS_OK(r) )
				d->work++;
		} else {
			d->leisure++;
			if( verbose > 3 )
				fprintf(stderr, "Missing filelist for %s\n", filekey);
		}
	} else if( RET_IS_OK(r) )
		d->leisure++;

	free(filekey);
	free(section);
	return r;
}

static retvalue genarchcontents(struct database *database, struct distribution *distribution, const char *architecture, struct release *release, bool_t onlyneeded) {
	retvalue r;
	struct target *target;
	char *contentsfilename;
	struct filetorelease *file;
	struct addcontentsdata data;
	const struct strlist *components;

	data.rate = distribution->contents.rate;
	data.work = data.leisure = 0;
	data.db = database;

	if( distribution->contents.components.count > 0 )
		components = &distribution->contents.components;
	else
		components = &distribution->components;

	if( onlyneeded ) {
		for( target=distribution->targets; target!=NULL;
				target=target->next ) {
			if( target->saved_wasmodified
				&& strcmp(target->architecture,architecture) == 0
				&& strcmp(target->packagetype,"deb") == 0
				&& strlist_in(components, target->component) )
					break;
		}
		if( target != NULL )
			onlyneeded = FALSE;
	}

	contentsfilename = mprintf("Contents-%s",architecture);
	if( contentsfilename == NULL )
		return RET_ERROR_OOM;
	r = release_startfile(release, contentsfilename,
			distribution->contents.compressions,
			onlyneeded, &file);
	if( !RET_IS_OK(r) ) {
		free(contentsfilename);
		return r;
	}
	if( verbose > 0 ) {
		printf(" generating %s...\n",contentsfilename);
	}
	free(contentsfilename);

	r = filelist_init(&data.contents);
	if( RET_WAS_ERROR(r) ) {
		release_abortfile(file);
		return r;
	}
	for( target=distribution->targets;target!=NULL;target=target->next ) {
		if( strcmp(target->packagetype,"deb") != 0 )
			continue;
		if( strcmp(target->architecture,architecture) != 0 )
			continue;
		if( !strlist_in(components, target->component) )
			continue;
		r = target_initpackagesdb(target, database);
		if( RET_WAS_ERROR(r) )
			break;
		r = packages_foreach(target->packages,addpackagetocontents,&data);
		(void)target_closepackagesdb(target);
		if( RET_WAS_ERROR(r) )
			break;
	}
	if( !RET_WAS_ERROR(r) )
		r = filelist_write(data.contents, file);
	if( RET_WAS_ERROR(r) )
		release_abortfile(file);
	else
		r = release_finishfile(release,file);
	filelist_free(data.contents);
	return r;
}

static retvalue genarchudebcontents(struct database *database, struct distribution *distribution, const char *architecture, struct release *release, bool_t onlyneeded) {
	retvalue r;
	struct target *target;
	char *contentsfilename;
	struct filetorelease *file;
	struct addcontentsdata data;
	const struct strlist *components;

	data.rate = distribution->contents.rate;
	data.work = data.leisure = 0;
	data.db = database;

	if( distribution->contents.ucomponents.count > 0 )
		components = &distribution->contents.ucomponents;
	else
		components = &distribution->udebcomponents;


	if( onlyneeded ) {
		for( target=distribution->targets; target!=NULL;
				target=target->next ) {
			if( target->saved_wasmodified
				&& strcmp(target->architecture,architecture) == 0
				&& strcmp(target->packagetype,"udeb") == 0
				&& strlist_in(components, target->component) )
				break;
		}
		if( target != NULL )
			onlyneeded = FALSE;
	}

	contentsfilename = mprintf("uContents-%s",architecture);
	if( contentsfilename == NULL )
		return RET_ERROR_OOM;
	r = release_startfile(release, contentsfilename,
			distribution->contents.compressions,
			onlyneeded, &file);
	if( !RET_IS_OK(r) ) {
		free(contentsfilename);
		return r;
	}
	if( verbose > 0 ) {
		printf(" generating %s...\n",contentsfilename);
	}
	free(contentsfilename);
	r = filelist_init(&data.contents);
	if( RET_WAS_ERROR(r) )
		return r;
	for( target=distribution->targets;target!=NULL;target=target->next ) {
		if( strcmp(target->packagetype,"udeb") != 0 )
			continue;
		if( strcmp(target->architecture,architecture) != 0 )
			continue;
		if( !strlist_in(components, target->component) )
			continue;
		r = target_initpackagesdb(target, database);
		if( RET_WAS_ERROR(r) )
			break;
		r = packages_foreach(target->packages,addpackagetocontents,&data);
		(void)target_closepackagesdb(target);
		if( RET_WAS_ERROR(r) )
			break;
	}
	if( !RET_WAS_ERROR(r) )
		r = filelist_write(data.contents, file);
	if( RET_WAS_ERROR(r) )
		release_abortfile(file);
	else
		r = release_finishfile(release,file);
	filelist_free(data.contents);
	return r;
}

retvalue contents_generate(struct database *database, struct distribution *distribution, struct release *release, bool_t onlyneeded) {
	retvalue result,r;
	int i;
	const struct strlist *architectures;

	result = RET_NOTHING;
	if( distribution->contents.architectures.count > 0 ) {
		architectures = &distribution->contents.architectures;
	} else {
		architectures = &distribution->architectures;
	}
	for( i = 0 ; i < architectures->count ; i++ ) {
		const char *architecture = architectures->values[i];
		if( strcmp(architecture,"source") == 0 )
			continue;

		if( !distribution->contents.flags.nodebs) {
			r = genarchcontents(database, distribution,
					architecture,release,onlyneeded);
			RET_UPDATE(result,r);
		}
		if( distribution->contents.flags.udebs) {
			r = genarchudebcontents(database, distribution,
					architecture,release,onlyneeded);
			RET_UPDATE(result,r);
		}
	}
	return result;
}


