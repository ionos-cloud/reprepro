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

extern int verbose;

void contentsoptions_done(struct contentsoptions *options) {
	strlist_done(&options->architectures);
	strlist_done(&options->components);
	strlist_done(&options->ucomponents);
}

struct distribution;

/* options are zerroed when called, when error is returned contentsopions_done
 * is called by the caller */
retvalue contentsoptions_parse(struct distribution *distribution, const char *chunk) {
	char *option,*e;
	retvalue r;
	long l;
	bool_t defaultcompressions;

	r = chunk_getvalue(chunk,"Contents",&option);
	if( RET_WAS_ERROR(r) ) {
		return r;
	} else if( r == RET_NOTHING ) {
		// check the other fields or just ignore them?
		distribution->contents.rate = 0;
		return RET_NOTHING;
	}
	assert( RET_IS_OK(r) );
	l = strtol(option, &e, 10);
	if( l == LONG_MAX || l >= INT_MAX ) {
		fprintf(stderr,"Too large rate value in Contents: line of %s: %s\n",
				distribution->codename,
				option);
		free(option);
		return RET_ERROR;
	}
	if( l <= 0 )
		distribution->contents.rate = 0;
	else
		distribution->contents.rate = l;
	defaultcompressions = TRUE;
	distribution->contents.compressions = IC_FLAG(ic_gzip);
	while( *e != '\0' && xisspace(*e) )
		e++;
	while( *e != '\0' ) {
		if( strncmp(e, "udebs", 5) == 0 &&
				(e[5] == '\0' || isspace(e[5])) ) {
			distribution->contents.flags.udebs = TRUE;
			e += 5;
		} else if( strncmp(e, "nodebs", 6) == 0 &&
				(e[6] == '\0' || isspace(e[6])) ) {
			distribution->contents.flags.nodebs = TRUE;
			e += 6;
		} else if( strncmp(e, ".bz2", 4) == 0 && 
#ifdef HAVE_LIBBZ2
				(e[4] == '\0' || isspace(e[4])) ) {
			if( defaultcompressions ) {
				defaultcompressions = FALSE;
				distribution->contents.compressions = 0;
			}
			distribution->contents.compressions |=
				IC_FLAG(ic_bzip2);
#else
			fprintf(stderr, "Warning: Ignoring request to generate .bz2'ed Contents files as compiled without libbz2.\n");
#endif
			e += 4;
		} else if( strncmp(e, ".gz", 3) == 0 && 
				(e[3] == '\0' || isspace(e[3])) ) {
			if( defaultcompressions ) {
				defaultcompressions = FALSE;
				distribution->contents.compressions = 0;
			}
			distribution->contents.compressions |=
				IC_FLAG(ic_gzip);
			e += 3;
		} else if( e[0] == '.' && (e[1] == '\0' || isspace(e[1])) ) {
			if( defaultcompressions ) {
				defaultcompressions = FALSE;
				distribution->contents.compressions = 0;
			}
			distribution->contents.compressions |=
				IC_FLAG(ic_uncompressed);
			e += 1;
		} else {
			fprintf(stderr, "Unknown Contents: option in %s: '%s'\n",
					distribution->codename, e);
			free(option);
			return RET_ERROR;
		}
		while( *e != '\0' && xisspace(*e) )
			e++;
	}
	free(option);
	if( distribution->contents.rate == 0 )
		return RET_NOTHING;
	r = chunk_getwordlist(chunk, "ContentsArchitectures",
			&distribution->contents.architectures);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		distribution->contents.architectures.count = 0;
		distribution->contents.architectures.values = NULL;
	} else {
		const char *missing;

		if( !strlist_subset(&distribution->architectures,
					&distribution->contents.architectures,
					&missing) ) {
			fprintf(stderr, "ContentsArchitectures of %s lists architecture %s not found in its Architectures: list!\n",
					distribution->codename,
					missing);
			return RET_ERROR;
		}
	}
	r = chunk_getwordlist(chunk, "ContentsComponents",
			&distribution->contents.components);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		distribution->contents.components.count = 0;
		distribution->contents.components.values = NULL;
	} else {
		const char *missing;

		if( !strlist_subset(&distribution->components,
					&distribution->contents.components,
					&missing) ) {
			fprintf(stderr, "ContentsComponents of %s lists component %s not found in its Components: list!\n",
					distribution->codename,
					missing);
			return RET_ERROR;
		}
	}
	r = chunk_getwordlist(chunk, "ContentsUComponents",
			&distribution->contents.ucomponents);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		distribution->contents.ucomponents.count = 0;
		distribution->contents.ucomponents.values = NULL;
	} else {
		const char *missing;

		if( !strlist_subset(&distribution->udebcomponents,
					&distribution->contents.ucomponents,
					&missing) ) {
			fprintf(stderr, "ContentsUComponents of %s lists udeb-component %s not found in its UComponents: list!\n",
					distribution->codename,
					missing);
			return RET_ERROR;
		}
	}
	return RET_OK;
}

struct addcontentsdata {
	int rate;
	size_t work, leisure;
	struct filelist_list *contents;
	filesdb files;
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
	r = files_getfilelist(d->files, filekey, package, d->contents);
	if( r == RET_NOTHING ) {
		if( d->rate <= 1 || d->work <= d->leisure/(d->rate-1) ) {
			if( verbose > 2 )
				fprintf(stderr, "Reading filelist for %s\n", filekey);
			r = files_genfilelist(d->files, filekey, package, d->contents);
			if( RET_IS_OK(r) )
				d->work++;
		} else {
			d->leisure++;
			if( verbose > 2 )
				fprintf(stderr, "Missing filelist for %s\n", filekey);
		}
	} else if( RET_IS_OK(r) )
		d->leisure++;

	free(filekey);
	free(section);		
	return r;
}

static retvalue genarchcontents(filesdb files, struct distribution *distribution, const char *dbdir, const char *architecture, struct release *release, bool_t onlyneeded) {
	retvalue r;
	struct target *target;
	char *contentsfilename;
	struct filetorelease *file;
	struct addcontentsdata data;
	const struct strlist *components;

	data.rate = distribution->contents.rate;
	data.work = data.leisure = 0;
	data.files = files;

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
		fprintf(stderr, " generating %s...\n",contentsfilename);
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
		r = target_initpackagesdb(target, dbdir);
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

static retvalue genarchudebcontents(filesdb files, struct distribution *distribution, const char *dbdir, const char *architecture, struct release *release, bool_t onlyneeded) {
	retvalue r;
	struct target *target;
	char *contentsfilename;
	struct filetorelease *file;
	struct addcontentsdata data;
	const struct strlist *components;

	data.rate = distribution->contents.rate;
	data.work = data.leisure = 0;
	data.files = files;

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
		fprintf(stderr, " generating %s...\n",contentsfilename);
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
		r = target_initpackagesdb(target, dbdir);
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

retvalue contents_generate(filesdb files, struct distribution *distribution, const char *dbdir, struct release *release, bool_t onlyneeded) {
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
			r = genarchcontents(files, distribution, dbdir,
					architecture,release,onlyneeded);
			RET_UPDATE(result,r);
		}
		if( distribution->contents.flags.udebs) {
			r = genarchudebcontents(files, distribution, dbdir,
					architecture,release,onlyneeded);
			RET_UPDATE(result,r);
		}
	}
	return result;
}
		

