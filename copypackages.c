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
#include "indexfile.h"
#include "files.h"
#include "target.h"
#include "terms.h"
#include "dpkgversions.h"
#include "tracking.h"
#include "filecntl.h"
#include "mprintf.h"
#include "copypackages.h"

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
		architecture_t architecture;
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

static retvalue list_prepareadd(struct database *database, struct package_list *list, struct target *target, const char *packagename, /*@null@*/const char *v, architecture_t package_architecture, const char *chunk) {
	char *version;
	char *source, *sourceversion;
	struct package *new IFSTUPIDCC(=NULL);
	retvalue r;
	int i;

	if( v == NULL ) {
		r = target->getversion(chunk, &version);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
	}
	r = target->getsourceandversion(chunk, packagename,
			&source, &sourceversion);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		free(version);
		return r;
	}
	r = list_newpackage(list, target, source, sourceversion,
			packagename, (v==NULL)?version:v, &new);
	free(source); source = NULL;
	free(sourceversion); sourceversion = NULL;
	if( v == NULL ) free(version); version = NULL;
	if( RET_WAS_ERROR(r) )
		return r;

	new->architecture = package_architecture;
	r = target->getinstalldata(target, new->name, new->version,
			package_architecture, chunk,
			&new->control, &new->filekeys, &new->origfiles);
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

static retvalue package_add(struct database *database, struct distribution *into, /*@null@*/trackingdb tracks, struct target *target, const struct package *package) {
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
			(tracks != NULL)?
			&trackingdata:NULL,
			package->architecture);
	RET_UPDATE(into->status, r);
	if( tracks != NULL ) {
		retvalue r2;

		r2 = trackingdata_finish(tracks, &trackingdata,
				database);
		RET_ENDUPDATE(r, r2);
	}
	return r;
}

static retvalue packagelist_add(struct database *database, struct distribution *into, const struct package_list *list) {
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
					package);
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

static retvalue copy_by_func(struct package_list *list, struct database *database, struct distribution *into, struct distribution *from, component_t component_atom, architecture_t architecture_atom, packagetype_t packagetype_atom, retvalue action(struct package_list*, struct database *, struct distribution *, struct distribution *, struct target *, struct target *, void *), void *data) {
	retvalue result, r;
	struct target *origtarget, *desttarget;

	result = RET_NOTHING;
	for( origtarget = from->targets ; origtarget != NULL ;
			origtarget = origtarget->next ) {
		if( !target_matches(origtarget,
				component_atom, architecture_atom, packagetype_atom) )
			continue;
		desttarget = distribution_gettarget(into,
				origtarget->component_atom,
				origtarget->architecture_atom,
				origtarget->packagetype_atom);
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
		architecture_t package_architecture;

		for( j = 0 ; j < i ; j++ )
			if( strcmp(d->argv[i], d->argv[j]) == 0 )
				break;
		if( j < i ) {
			if( verbose >= 0 && ! d->warnedabout[j])
				fprintf(stderr, "Hint: '%s' was listed multiple times, ignoring all but first!\n", d->argv[i]);
			d->warnedabout[j] = true;
			continue;
		}

		r = table_getrecord(fromtarget->packages, name, &chunk);
		if( r == RET_NOTHING )
			continue;
		RET_ENDUPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			break;
		r = fromtarget->getarchitecture(chunk, &package_architecture);
		RET_ENDUPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			break;
		r = list_prepareadd(database, list, desttarget,
				name, NULL, package_architecture, chunk);
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

retvalue copy_by_name(struct database *database, struct distribution *into, struct distribution *from, int argc, const char **argv, component_t component, architecture_t architecture, packagetype_t packagetype) {
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
	r = packagelist_add(database, into, &list);
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
		architecture_t package_architecture;

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
		r = fromtarget->getarchitecture(chunk, &package_architecture);
		if( RET_WAS_ERROR(r) ) {
			result = r;
			break;
		}
		r = list_prepareadd(database, list, desttarget,
				packagename, NULL, package_architecture, chunk);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue copy_by_source(struct database *database, struct distribution *into, struct distribution *from, int argc, const char **argv, component_t component, architecture_t architecture, packagetype_t packagetype) {
	struct package_list list;
	struct namelist names = { argc, argv, NULL };
	retvalue r;

	memset(&list, 0, sizeof(list));
	// TODO: implement fast way by looking at source tracking
	// (also allow copying .changes and .logs)
	r = copy_by_func(&list, database, into, from, component, architecture, packagetype, by_source, &names);
	if( !RET_IS_OK(r) )
		return r;
	r = packagelist_add(database, into, &list);
	packagelist_done(&list);
	return r;
}

static retvalue by_formula(struct package_list *list, struct database *database, UNUSED(struct distribution *into), UNUSED(struct distribution *from), struct target *desttarget, struct target *fromtarget, void *data) {
	term *condition = data;
	struct target_cursor iterator IFSTUPIDCC(=TARGET_CURSOR_ZERO);
	const char *packagename, *chunk;
	architecture_t package_architecture;
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
		r = fromtarget->getarchitecture(chunk, &package_architecture);
		if( RET_WAS_ERROR(r) ) {
			result = r;
			break;
		}
		r = list_prepareadd(database, list, desttarget,
				packagename, NULL, package_architecture, chunk);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue copy_by_formula(struct database *database, struct distribution *into, struct distribution *from, const char *filter, component_t component, architecture_t architecture, packagetype_t packagetype) {
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
	r = packagelist_add(database, into, &list);
	packagelist_done(&list);
	return r;
}

static retvalue choose_by_name(UNUSED(struct target *target), const char *packagename, UNUSED(const char *version), UNUSED(const char *chunk), void *privdata) {
	const struct namelist *l = privdata;
	int i;

	for( i = 0 ; i < l->argc ; i++ ) {
		if( strcmp(packagename, l->argv[i]) == 0 )
			break;
	}
	if( i >= l->argc )
		return RET_NOTHING;
	return RET_OK;
}

static retvalue choose_by_source(struct target *target, const char *packagename, UNUSED(const char *versiondummy), const char *chunk, void *privdata) {
	const struct namelist *l = privdata;
	char *source, *sourceversion;
	retvalue r;

	// TODO: why doesn't this use version?
	r = target->getsourceandversion(chunk, packagename,
			&source, &sourceversion);
	if( !RET_IS_OK(r) ) {
		return r;
	}
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
	return RET_OK;
}

static retvalue choose_by_condition(UNUSED(struct target *target), UNUSED(const char *packagename), UNUSED(const char *version), const char *chunk, void *privdata) {
	term *condition = privdata;

	return term_decidechunk(condition, chunk);
}

retvalue copy_from_file(struct database *database, struct distribution *into, component_t component_atom, architecture_t architecture_atom, packagetype_t packagetype_atom, const char *filename, int argc, const char **argv) {
	struct indexfile *i;
	retvalue result, r;
	struct target *target;
	struct package_list list;
	struct namelist d = {argc, argv, NULL};
	char *packagename, *version;
	architecture_t package_architecture;
	const char *control;

	assert( atom_defined(architecture_atom) );
	assert( atom_defined(component_atom) );
	assert( atom_defined(packagetype_atom) );

	memset(&list, 0, sizeof(list));
	target = distribution_gettarget(into,
			component_atom, architecture_atom, packagetype_atom);
	if( target == NULL ) {
		if( !atomlist_in(&into->architectures, architecture_atom) ) {
			fprintf(stderr, "Distribution '%s' does not contain architecture '%s!'\n",
					into->codename, atoms_architectures[architecture_atom]);
		}
		if( packagetype_atom != pt_udeb ) {
			if( !atomlist_in(&into->components, component_atom) ) {
				fprintf(stderr,
"Distribution '%s' does not contain component '%s!'\n",
						into->codename, atoms_components[component_atom]);
			}
		} else {
			if( !atomlist_in(&into->udebcomponents, component_atom) ) {
				fprintf(stderr,
"Distribution '%s' does not contain udeb component '%s!'\n",
						into->codename, atoms_components[component_atom]);
			}
		}
		/* -A source needing -T dsc and vice versa already checked
		 * in main.c */
		fprintf(stderr,
"No matching part of distribution '%s' found!\n",
						into->codename);
		return RET_ERROR;
	}
	result = indexfile_open(&i, filename, c_none);
	if( !RET_IS_OK(result) )
		return result;
	result = RET_NOTHING;
	while( indexfile_getnext(i, &packagename, &version, &control,
				&package_architecture, target, false) ) {
		result = choose_by_name(target,
				packagename, version, control, &d);
		if( RET_IS_OK(result) )
			result = list_prepareadd(database, &list, target,
					packagename, version,
					package_architecture, control);
		free(packagename);
		free(version);
		if( RET_WAS_ERROR(result) )
			break;
	}
	r = indexfile_close(i);
	RET_ENDUPDATE(result, r);
	if( RET_IS_OK(result) )
		result = packagelist_add(database, into, &list);
	packagelist_done(&list);
	return result;
}

typedef retvalue chooseaction(struct target *, const char *, const char *, const char *, void *);

static retvalue restore_from_snapshot(struct database *database, struct distribution *into, component_t component_atom, architecture_t architecture_atom, packagetype_t packagetype_atom, const char *snapshotname, chooseaction action, void *d) {
	retvalue result, r;
	struct package_list list;
	struct target *target;
	char *basedir;
	enum compression compression;
	architecture_t package_architecture;

	basedir = calc_snapshotbasedir(into->codename, snapshotname);
	if( FAILEDTOALLOC(basedir) )
		return RET_ERROR_OOM;

	memset(&list, 0, sizeof(list));
	result = RET_NOTHING;
	for( target = into->targets ; target != NULL ;
			target = target->next ) {
		char *filename, *packagename, *version;
		const char *control;
		struct indexfile *i;

		if( !target_matches(target,
				component_atom, architecture_atom, packagetype_atom) )
			continue;

		/* we do not know what compressions where used back then
		 * and not even how the file was named, just look for
		 * how the file is named now and try all readable
		 * compressions */

		compression = c_none;
		filename = calc_dirconcat3(
				basedir, target->relativedirectory,
				target->exportmode->filename);
		if( filename != NULL && !isregularfile(filename) ) {
			/* no uncompressed file found, try .gz */
			free(filename);
			compression = c_gzip;
			filename = mprintf("%s/%s/%s.gz",
					basedir, target->relativedirectory,
					target->exportmode->filename);
		}
#ifdef HAVE_LIBBZ2
		if( filename != NULL && !isregularfile(filename) ) {
			/* no uncompressed or .gz file found, try .bz2 */
			free(filename);
			compression = c_bzip2;
			filename = mprintf("%s/%s/%s.bz2",
					basedir, target->relativedirectory,
					target->exportmode->filename);
		}
#endif
		if( filename != NULL && !isregularfile(filename) ) {
			free(filename);
			fprintf(stderr,
"Could not find '%s/%s/%s' nor '%s/%s/%s.gz',\n"
"ignoring that part of the snapshot.\n",
					basedir, target->relativedirectory,
					target->exportmode->filename,
					basedir, target->relativedirectory,
					target->exportmode->filename);
			continue;
		}
		if( FAILEDTOALLOC(filename) ) {
			result = RET_ERROR_OOM;
			break;
		}
		result = indexfile_open(&i, filename, compression);
		if( !RET_IS_OK(result) )
			break;
		while( indexfile_getnext(i, &packagename, &version, &control,
					&package_architecture,
					target, false) ) {
			result = action(target,
					packagename, version, control, d);
			if( RET_IS_OK(result) )
				result = list_prepareadd(database, &list, target,
						packagename, version,
						package_architecture, control);
			free(packagename);
			free(version);
			if( RET_WAS_ERROR(result) )
				break;
		}
		r = indexfile_close(i);
		RET_ENDUPDATE(result, r);
		free(filename);
		if( RET_WAS_ERROR(result) )
			break;
	}
	free(basedir);
	if( !RET_IS_OK(result) )
		return result;
	r = packagelist_add(database, into, &list);
	packagelist_done(&list);
	return r;
}

retvalue restore_by_name(struct database *database, struct distribution *into, component_t component, architecture_t architecture, packagetype_t packagetype, const char *snapshotname, int argc, const char **argv) {
	struct namelist d = {argc, argv, NULL};
	return restore_from_snapshot(database, into,
			component, architecture, packagetype,
			snapshotname, choose_by_name, &d);
}

retvalue restore_by_source(struct database *database, struct distribution *into, component_t component, architecture_t architecture, packagetype_t packagetype, const char *snapshotname, int argc, const char **argv) {
	struct namelist d = {argc, argv, NULL};
	return restore_from_snapshot(database, into,
			component, architecture, packagetype,
			snapshotname, choose_by_source, &d);
}

retvalue restore_by_formula(struct database *database, struct distribution *into, component_t component, architecture_t architecture, packagetype_t packagetype, const char *snapshotname, const char *filter) {
	term *condition;
	retvalue r;

	r = term_compile(&condition, filter,
			T_OR|T_BRACKETS|T_NEGATION|T_VERSION|T_NOTEQUAL);
	if( !RET_IS_OK(r) ) {
		return r;
	}
	r = restore_from_snapshot(database, into,
			component, architecture, packagetype,
			snapshotname, choose_by_condition, condition);
	term_free(condition);
	return r;
}
