/*  This file is part of "reprepro"
 *  Copyright (C) 2008 Bernhard R. Link
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

#include <sys/types.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include "error.h"
#include "ignore.h"
#include "strlist.h"
#include "chunks.h"
#include "files.h"
#include "target.h"
#include "terms.h"
#include "dpkgversions.h"
#include "tracking.h"
#include "filecntl.h"
#include "mprintf.h"
#include "copypackages.h"

extern int verbose;

struct target_package_list {
	struct target_package_list *next;
	struct target *target;
	struct package {
		/*@null@*/struct package *next;
		char *name;
		char *version;
		char *sourcename;
		char *sourceversion;
		char *control;
		struct checksumsarray origfiles;
		struct strlist filekeys;
		enum filetype trackingtype;
	} *packages;
};

struct package_list {
	/*@null@*/struct target_package_list *targets;
};

static retvalue list_newpackage(struct package_list *list, struct target *target, const char *sourcename, const char *sourceversion, const char *packagename, const char *packageversion, /*@out@*/struct package **package_p) {
	struct target_package_list *t, **t_p;
	struct package *package, **p_p;
	int c;

	t_p = &list->targets;
	while( *t_p != NULL && (*t_p)->target != target )
		t_p = &(*t_p)->next;
	if( *t_p == NULL ) {
		t = calloc(1, sizeof(struct target_package_list));
		if( FAILEDTOALLOC(t) )
			return RET_ERROR_OOM;
		t->target = target;
		t->next = *t_p;
		*t_p = t;
	} else
		t = *t_p;

	p_p = &t->packages;
	while( *p_p != NULL && ( c = strcmp(packagename, (*p_p)->name) ) < 0 )
		p_p = &(*p_p)->next;
	if( *p_p != NULL && c == 0 ) {
		// TODO: improve this message..., or some context elsewhere
		fprintf(stderr, "Multiple occurences of package '%s'!\n",
				packagename);
		return RET_ERROR_EXIST;
	}
	package = calloc(1, sizeof(struct package));
	if( FAILEDTOALLOC(package) )
		return RET_ERROR_OOM;
	package->name = strdup(packagename);
	if( FAILEDTOALLOC(package->name) ) {
		free(package);
		return RET_ERROR_OOM;
	}
	package->version = strdup(packageversion);
	if( FAILEDTOALLOC(package->version) ) {
		free(package->name);
		free(package);
		return RET_ERROR_OOM;
	}
	package->sourcename = strdup(sourcename);
	if( FAILEDTOALLOC(package->sourcename) ) {
		free(package->name);
		free(package->version);
		free(package);
		return RET_ERROR_OOM;
	}
	package->sourceversion = strdup(sourceversion);
	if( FAILEDTOALLOC(package->sourceversion) ) {
		free(package->name);
		free(package->version);
		free(package->sourcename);
		free(package);
		return RET_ERROR_OOM;
	}
	package->next = *p_p;
	*p_p = package;
	*package_p = package;
	return RET_OK;
}

static void package_free(/*@only@*/struct package *package) {
	if( package == NULL )
		return;

	free(package->name);
	free(package->version);
	free(package->sourcename);
	free(package->sourceversion);
	free(package->control);
	checksumsarray_done(&package->origfiles);
	strlist_done(&package->filekeys);
	free(package);
}

static void list_cancelpackage(struct package_list *list, /*@only@*/struct package *package) {
	struct target_package_list *target;
	struct package **p_p;

	assert( package != NULL );

	for( target = list->targets ; target != NULL ; target = target->next ) {
		p_p = &target->packages;
		while( *p_p != NULL && *p_p != package )
			p_p = &(*p_p)->next;
		if( *p_p == package ) {
			*p_p = package->next;
			package_free(package);
			return;
		}
	}
	assert( package == NULL );
}

static retvalue list_prepareadd(struct database *database, struct package_list *list, struct target *target, const char *chunk) {
	char *packagename, *version;
	char *source, *sourceversion;
	struct package *new IFSTUPIDCC(=NULL);
	retvalue r;
	int i;

	r = target->getname(chunk, &packagename);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;
	r = target->getversion(chunk, &version);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		free(packagename);
		return r;
	}
	r = target->getsourceandversion(chunk, packagename,
			&source, &sourceversion);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		free(packagename);
		free(version);
		return r;
	}
	r = list_newpackage(list, target, source, sourceversion,
			packagename, version, &new);
	free(source); source = NULL;
	free(sourceversion); sourceversion = NULL;
	free(packagename); packagename = NULL;
	free(version); version = NULL;
	if( RET_WAS_ERROR(r) )
		return r;

	r = target->getinstalldata(target, new->name, new->version, chunk,
			&new->control, &new->filekeys, &new->origfiles,
			&new->trackingtype);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		list_cancelpackage(list, new);
		return r;
	}
	assert( new->filekeys.count == new->origfiles.names.count );
	for( i = 0 ; i < new->filekeys.count ; i++ ) {
		const char *newfilekey = new->filekeys.values[i];
		const char *oldfilekey = new->origfiles.names.values[i];
		const struct checksums *checksums = new->origfiles.checksums[i];

		r = files_canadd(database, newfilekey, checksums);
		/* normaly it should just already have that file,
		 * in which case we have nothing to do: */
		if( r == RET_NOTHING )
			continue;
		/* otherwise try to cope with it */
		if( r == RET_ERROR_WRONG_MD5 ) {
			if( strcmp(newfilekey, oldfilekey) == 0 ) {
				fprintf(stderr,
"Error: package %s version %s lists different checksums than in the pool!\n",
					new->name, new->version);
			} else {
				fprintf(stderr,
"Error: package %s version %s needs '%s' which previously was '%s',\n"
"but the new file is already listed with different checksums!\n",
					new->name, new->version,
					newfilekey, oldfilekey);
			}
		}
		if( RET_WAS_ERROR(r) ) {
			list_cancelpackage(list, new);
			return r;
		}
		assert( RET_IS_OK(r) );
		if( strcmp(newfilekey, oldfilekey) == 0 ) {
			fprintf(stderr,
"Error: package %s version %s lists file %s not yet in the pool!\n",
				new->name, new->version, newfilekey);
			list_cancelpackage(list, new);
			return RET_ERROR_MISSING;
		}
		// TODO:
		// check new
		// - if exists and other checksums delete
		// - if exists and correct checksums use
		// otherwise check old
		// - if exists and other checksums bail out
		// - if exists and correct checksum, hardlink/copy
		fprintf(stderr,
"Error: cannot yet deal with files changing their position\n"
"(%s vs %s in %s version %s)\n",
					newfilekey, oldfilekey,
					new->name, new->version);
		list_cancelpackage(list, new);
		return RET_ERROR_MISSING;
	}
	return RET_OK;
}

static retvalue package_add(struct database *database, struct distribution *into, /*@null@*/trackingdb tracks, struct target *target, const struct package *package, struct strlist *dereferencedfilekeys) {
	struct trackingdata trackingdata;
	retvalue r;

	if( verbose >= 1 ) {
		printf("Adding '%s' '%s' to '%s'.\n",
				package->name, package->version,
				target->identifier);
	}

	r = files_expectfiles(database,
			&package->filekeys,
			package->origfiles.checksums);
	if( RET_WAS_ERROR(r) )
		return r;
	if( interrupted() )
		return RET_ERROR_INTERRUPTED;
	if( tracks != NULL ) {
		r = trackingdata_summon(tracks, package->sourcename,
				package->version, &trackingdata);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	r = target_addpackage(target,
			into->logger, database,
			package->name, package->version,
			package->control,
			&package->filekeys, NULL, true,
			dereferencedfilekeys,
			(tracks != NULL)?
			&trackingdata:NULL,
			package->trackingtype);
	RET_UPDATE(into->status, r);
	if( tracks != NULL ) {
		retvalue r2;

		r2 = trackingdata_finish(tracks, &trackingdata,
				database, dereferencedfilekeys);
		RET_ENDUPDATE(r, r2);
	}
	return r;
}

static retvalue packagelist_add(struct database *database, struct distribution *into, const struct package_list *list, struct strlist *dereferencedfilekeys) {
	retvalue result, r;
	struct target_package_list *tpl;
	struct package *package;
	trackingdb tracks;

	r = distribution_prepareforwriting(into);
	if( RET_WAS_ERROR(r) )
		return r;

	if( into->tracking != dt_NONE ) {
		r = tracking_initialize(&tracks, database, into, false);
		if( RET_WAS_ERROR(r) )
			return r;
	} else
		tracks = NULL;

	result = RET_NOTHING;
	for( tpl = list->targets; tpl != NULL ; tpl = tpl->next ) {
		struct target *target = tpl->target;

		r = target_initpackagesdb(target, database, READWRITE);
		RET_ENDUPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			break;
		for( package = tpl->packages; package != NULL ; package = package->next ) {
			r = package_add(database, into, tracks, target,
					package, dereferencedfilekeys);
			RET_UPDATE(result, r);
		}
		r = target_closepackagesdb(target);
		RET_UPDATE(into->status, r);
		RET_ENDUPDATE(result, r);
	}
	r = tracking_done(tracks);
	RET_ENDUPDATE(result, r);
	return result;
}

static retvalue copy_by_func(struct package_list *list, struct database *database, struct distribution *into, struct distribution *from, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, retvalue action(struct package_list*, struct database *, struct distribution *, struct distribution *, struct target *, struct target *, void *), void *data) {
	retvalue result, r;
	struct target *origtarget, *desttarget;

	result = RET_NOTHING;
	for( origtarget = from->targets ; origtarget != NULL ;
			origtarget = origtarget->next ) {
		if( !target_matches(origtarget,
				component, architecture, packagetype) )
			continue;
		desttarget = distribution_gettarget(into,
				origtarget->component,
				origtarget->architecture,
				origtarget->packagetype);
		if( desttarget == NULL ) {
			if( verbose > 2 )
				printf(
"Not looking into '%s' as no matching target in '%s'!\n",
					origtarget->identifier,
					into->codename);
			continue;
		}
		r = action(list, database, into, from, desttarget, origtarget, data);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(result) )
			return result;
	}
	return result;
}

struct namelist {
	int argc;
	const char **argv;
	bool *warnedabout;
};

static retvalue by_name(struct package_list *list, struct database *database, UNUSED(struct distribution *into), UNUSED(struct distribution *from), struct target *desttarget, struct target *fromtarget, void *data) {
	struct namelist *d = data;
	retvalue result, r;
	int i, j;

	r = target_initpackagesdb(fromtarget, database, READONLY);
	if( RET_WAS_ERROR(r) )
		return r;
	result = RET_NOTHING;
	for( i = 0 ; i < d->argc ; i++ ) {
		const char *name = d->argv[i];
		char *chunk;

		for( j = 0 ; j < i ; j++ )
			if( strcmp(d->argv[i], d->argv[j]) == 0 )
				break;
		if( j < i ) {
			if( verbose > 0 && ! d->warnedabout[j])
				fprintf(stderr, "Hint: '%s' was listed multiple times listed, ignoring all but first!\n", d->argv[i]);
			d->warnedabout[j] = true;
			continue;
		}

		r = table_getrecord(fromtarget->packages, name, &chunk);
		if( r == RET_NOTHING )
			continue;
		RET_ENDUPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			break;
		r = list_prepareadd(database, list, desttarget, chunk);
		free(chunk);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	r = target_closepackagesdb(fromtarget);
	RET_ENDUPDATE(result, r);
	return result;
}

static void packagelist_done(struct package_list *list) {
	struct target_package_list *target;
	struct package *package;

	while( (target = list->targets) != NULL ) {
		list->targets = target->next;

		while( (package = target->packages) != NULL ) {
			target->packages = package->next;

			package_free(package);
		}
		free(target);
	}
}

retvalue copy_by_name(struct database *database, struct distribution *into, struct distribution *from, int argc, const char **argv, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, struct strlist *dereferenced) {
	struct package_list list;
	struct namelist names = { argc, argv, calloc(argc, sizeof(bool)) };
	retvalue r;

	if( FAILEDTOALLOC(names.warnedabout) )
		return RET_ERROR_OOM;

	memset(&list, 0, sizeof(list));
	r = copy_by_func(&list, database, into, from, component, architecture, packagetype, by_name, &names);
	free(names.warnedabout);
	if( !RET_IS_OK(r) )
		return r;
	r = packagelist_add(database, into, &list, dereferenced);
	packagelist_done(&list);
	return r;
}

static retvalue by_source(struct package_list *list, struct database *database, UNUSED(struct distribution *into), UNUSED(struct distribution *from), struct target *desttarget, struct target *fromtarget, void *data) {
	struct namelist *d = data;
	struct target_cursor iterator IFSTUPIDCC(=TARGET_CURSOR_ZERO);
	const char *packagename, *chunk;
	retvalue result, r;

	assert( d->argc > 0 );

	r = target_openiterator(fromtarget, database, READONLY, &iterator);
	assert( r != RET_NOTHING );
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( target_nextpackage(&iterator, &packagename, &chunk) ) {
		char *source, *sourceversion;

		r = fromtarget->getsourceandversion(chunk, packagename,
				&source, &sourceversion);
		if( r == RET_NOTHING )
			continue;
		if( RET_WAS_ERROR(r) ) {
			result = r;
			break;
		}
		/* only include if source name matches */
		if( strcmp(source, d->argv[0]) != 0 ) {
			free(source); free(sourceversion);
			continue;
		}
		if( d->argc > 1 ) {
			int i, c;

			i = d->argc;
			while( --i > 0 ) {
				r = dpkgversions_cmp(sourceversion,
						d->argv[i], &c);
				assert( r != RET_NOTHING );
				if( RET_WAS_ERROR(r) ) {
					free(source); free(sourceversion);
					(void)target_closeiterator(&iterator);
					return r;
				}
				if( c == 0 )
					break;
			}
			/* there are source versions specified and
			 * the source version of this package differs */
			if( i == 0 ) {
				free(source); free(sourceversion);
				continue;
			}
		}
		free(source); free(sourceversion);
		r = list_prepareadd(database, list, desttarget, chunk);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue copy_by_source(struct database *database, struct distribution *into, struct distribution *from, int argc, const char **argv, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, struct strlist *dereferenced) {
	struct package_list list;
	struct namelist names = { argc, argv };
	retvalue r;

	memset(&list, 0, sizeof(list));
	// TODO: implement fast way by looking at source tracking
	// (also allow copying .changes and .logs)
	r = copy_by_func(&list, database, into, from, component, architecture, packagetype, by_source, &names);
	if( !RET_IS_OK(r) )
		return r;
	r = packagelist_add(database, into, &list, dereferenced);
	packagelist_done(&list);
	return r;
}

static retvalue by_formula(struct package_list *list, struct database *database, UNUSED(struct distribution *into), UNUSED(struct distribution *from), struct target *desttarget, struct target *fromtarget, void *data) {
	term *condition = data;
	struct target_cursor iterator IFSTUPIDCC(=TARGET_CURSOR_ZERO);
	const char *packagename, *chunk;
	retvalue result, r;

	r = target_openiterator(fromtarget, database, READONLY, &iterator);
	assert( r != RET_NOTHING );
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( target_nextpackage(&iterator, &packagename, &chunk) ) {
		r = term_decidechunk(condition, chunk);
		if( r == RET_NOTHING )
			continue;
		if( RET_WAS_ERROR(r) ) {
			result = r;
			break;
		}
		r = list_prepareadd(database, list, desttarget, chunk);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue copy_by_formula(struct database *database, struct distribution *into, struct distribution *from, const char *filter, /*@null@*/const char *component, /*@null@*/const char *architecture, /*@null@*/const char *packagetype, struct strlist *dereferenced) {
	struct package_list list;
	term *condition;
	retvalue r;

	memset(&list, 0, sizeof(list));

	r = term_compile(&condition, filter,
			T_OR|T_BRACKETS|T_NEGATION|T_VERSION|T_NOTEQUAL);
	if( !RET_IS_OK(r) ) {
		return r;
	}
	r = copy_by_func(&list, database, into, from, component, architecture, packagetype, by_formula, condition);
	term_free(condition);
	if( !RET_IS_OK(r) )
		return r;
	r = packagelist_add(database, into, &list, dereferenced);
	packagelist_done(&list);
	return r;
}

struct copy_from_file_data {
	struct package_list *list;
	struct database *database;
	struct distribution *distribution;
	const char *filename;
	struct target *target;
	void *privdata;
};

static retvalue choose_by_name(void *data, const char *chunk) {
	struct copy_from_file_data *d = data;
	const struct namelist *l = d->privdata;
	char *packagename;
	int i;
	retvalue r;

	r = d->target->getname(chunk, &packagename);
	if( !RET_IS_OK(r) )
		return r;

	for( i = 0 ; i < l->argc ; i++ ) {
		if( strcmp(packagename, l->argv[i]) == 0 )
			break;
	}
	free(packagename);
	if( i >= l->argc )
		return RET_NOTHING;
	return list_prepareadd(d->database, d->list, d->target, chunk);
}

static retvalue choose_by_source(void *data, const char *chunk) {
	struct copy_from_file_data *d = data;
	const struct namelist *l = d->privdata;
	char *packagename, *source, *sourceversion;
	retvalue r;

	r = d->target->getname(chunk, &packagename);
	if( !RET_IS_OK(r) )
		return r;
	r = d->target->getsourceandversion(chunk, packagename,
			&source, &sourceversion);
	free(packagename);
	if( !RET_IS_OK(r) )
		return r;
	assert( l->argc > 0 );
	/* only include if source name matches */
	if( strcmp(source, l->argv[0]) != 0 ) {
		free(source); free(sourceversion);
		return RET_NOTHING;
	}
	if( l->argc > 1 ) {
		int i, c;

		i = l->argc;
		while( --i > 0 ) {
			r = dpkgversions_cmp(sourceversion,
					l->argv[i], &c);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) ) {
				free(source); free(sourceversion);
				return r;
			}
			if( c == 0 )
				break;
		}
		/* there are source versions specified and
		 * the source version of this package differs */
		if( i == 0 ) {
			free(source); free(sourceversion);
			return RET_NOTHING;
		}
	}
	free(source); free(sourceversion);
	return list_prepareadd(d->database, d->list, d->target, chunk);
}

static retvalue choose_by_condition(void *data, const char *chunk) {
	struct copy_from_file_data *d = data;
	term *condition = d->privdata;
	retvalue r;

	r = term_decidechunk(condition, chunk);
	if( !RET_IS_OK(r) )
		return r;
	return list_prepareadd(d->database, d->list, d->target, chunk);
}

retvalue copy_from_file(struct database *database, struct distribution *into, const char *component, const char *architecture, const char *packagetype, const char *filename, int argc, const char **argv, struct strlist *dereferenced) {
	retvalue result, r;
	struct target *target;
	struct copy_from_file_data data;
	struct package_list list;
	struct namelist d = {argc, argv};

	assert( architecture != NULL );
	assert( component != NULL );
	assert( packagetype != NULL );

	memset(&list, 0, sizeof(list));
	target = distribution_gettarget(into,
			component, architecture, packagetype);
	if( target == NULL ) {
		if( !strlist_in(&into->architectures, architecture) ) {
			fprintf(stderr, "Distribution '%s' does not contain architecture '%s!'\n",
					into->codename, architecture);
		}
		if( strcmp(packagetype, "udeb") != 0 ) {
			if( !strlist_in(&into->components, component) ) {
				fprintf(stderr,
"Distribution '%s' does not contain component '%s!'\n",
						into->codename, component);
			}
		} else {
			if( !strlist_in(&into->udebcomponents, component) ) {
				fprintf(stderr,
"Distribution '%s' does not contain udeb component '%s!'\n",
						into->codename, component);
			}
		}
		/* -A source needing -T dsc and vice versa already checked
		 * in main.c */
		fprintf(stderr,
"No matching part of distribution '%s' found!\n",
						into->codename);
		return RET_ERROR;
	}
	data.list = &list;
	data.database = database;
	data.distribution = into;
	data.filename = filename;
	data.target = target;
	data.privdata = &d;
	result = chunk_foreach(filename, choose_by_name, &data, false);
	if( !RET_IS_OK(result) )
		return result;
	r = packagelist_add(database, into, &list, dereferenced);
	packagelist_done(&list);
	return r;
}

static retvalue restore_from_snapshot(struct database *database, struct distribution *into, const char *component, const char *architecture, const char *packagetype, const char *snapshotname, chunkaction action, void *d, struct strlist *dereferenced) {
	retvalue result, r;
	struct copy_from_file_data data;
	struct package_list list;
	char *basedir;

	basedir = calc_snapshotbasedir(into->codename, snapshotname);
	if( FAILEDTOALLOC(basedir) )
		return RET_ERROR_OOM;

	memset(&list, 0, sizeof(list));
	data.list = &list;
	data.database = database;
	data.distribution = into;
	data.privdata = d;
	result = RET_NOTHING;
	for( data.target = into->targets ; data.target != NULL ;
			data.target = data.target->next ) {
		char *filename;

		if( !target_matches(data.target,
				component, architecture, packagetype) )
			continue;

		/* we do not know what compressions where used back then
		 * and not even how the file was named, just look for
		 * how the file is named now and try all readable
		 * compressions */

		filename = calc_dirconcat3(
				basedir, data.target->relativedirectory,
				data.target->exportmode->filename);
		if( filename != NULL && !isregularfile(filename) ) {
			/* no uncompressed file found, try .gz */
			free(filename);
			filename = mprintf("%s/%s/%s.gz",
					basedir, data.target->relativedirectory,
					data.target->exportmode->filename);
		}
		if( filename != NULL && !isregularfile(filename) ) {
			free(filename);
			fprintf(stderr,
"Could not find '%s/%s/%s' nor '%s/%s/%s.gz',\n"
"ignoring that part of the snapshot.\n",
					basedir, data.target->relativedirectory,
					data.target->exportmode->filename,
					basedir, data.target->relativedirectory,
					data.target->exportmode->filename);
			continue;
		}
		if( FAILEDTOALLOC(filename) ) {
			free(basedir);
			return RET_ERROR_OOM;
		}

		data.filename = filename;
		r = chunk_foreach(filename, action, &data, false);
		free(filename);
		if( RET_WAS_ERROR(r) ) {
			free(basedir);
			return r;
		}
		RET_UPDATE(result, r);
	}
	free(basedir);
	if( !RET_IS_OK(result) )
		return result;
	r = packagelist_add(database, into, &list, dereferenced);
	packagelist_done(&list);
	return r;
}

retvalue restore_by_name(struct database *database, struct distribution *into, const char *component, const char *architecture, const char *packagetype, const char *snapshotname, int argc, const char **argv, struct strlist *dereferenced) {
	struct namelist d = {argc, argv};
	return restore_from_snapshot(database, into,
			component, architecture, packagetype,
			snapshotname, choose_by_name, &d, dereferenced);
}

retvalue restore_by_source(struct database *database, struct distribution *into, const char *component, const char *architecture, const char *packagetype, const char *snapshotname, int argc, const char **argv, struct strlist *dereferenced) {
	struct namelist d = {argc, argv};
	return restore_from_snapshot(database, into,
			component, architecture, packagetype,
			snapshotname, choose_by_source, &d, dereferenced);
}

retvalue restore_by_formula(struct database *database, struct distribution *into, const char *component, const char *architecture, const char *packagetype, const char *snapshotname, const char *filter, struct strlist *dereferenced) {
	term *condition;
	retvalue r;

	r = term_compile(&condition, filter,
			T_OR|T_BRACKETS|T_NEGATION|T_VERSION|T_NOTEQUAL);
	if( !RET_IS_OK(r) ) {
		return r;
	}
	r = restore_from_snapshot(database, into,
			component, architecture, packagetype,
			snapshotname, choose_by_condition, condition,
			dereferenced);
	term_free(condition);
	return r;
}
