/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007,2008 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "database.h"
#include "reference.h"
#include "binaries.h"
#include "sources.h"
#include "names.h"
#include "dirs.h"
#include "dpkgversions.h"
#include "tracking.h"
#include "log.h"
#include "files.h"
#include "target.h"

static char *calc_identifier(const char *codename, component_t component, architecture_t architecture, packagetype_t packagetype) {
	assert( strchr(codename, '|') == NULL );
	assert( codename != NULL ); assert( atom_defined(component) );
	assert( atom_defined(architecture) );
	assert( atom_defined(packagetype) );
	if( packagetype == pt_udeb )
		return mprintf("u|%s|%s|%s", codename,
				atoms_components[component],
				atoms_architectures[architecture]);
	else
		return mprintf("%s|%s|%s", codename,
				atoms_components[component],
				atoms_architectures[architecture]);
}


static retvalue target_initialize(const char *codename, component_t component, architecture_t architecture, packagetype_t packagetype, get_version getversion, get_installdata getinstalldata, get_architecture getarchitecture, get_filekeys getfilekeys, get_checksums getchecksums, get_sourceandversion getsourceandversion, do_reoverride doreoverride, do_retrack doretrack, /*@null@*//*@only@*/char *directory, /*@dependent@*/const struct exportmode *exportmode, /*@out@*/struct target **d) {
	struct target *t;

	assert(exportmode != NULL);
	if( directory == NULL )
		return RET_ERROR_OOM;

	t = calloc(1,sizeof(struct target));
	if( t == NULL ) {
		free(directory);
		return RET_ERROR_OOM;
	}
	t->relativedirectory = directory;
	t->exportmode = exportmode;
	t->codename = strdup(codename);
	assert( atom_defined(component) );
	t->component_atom = component;
	assert( atom_defined(architecture) );
	t->architecture_atom = architecture;
	assert( atom_defined(packagetype) );
	t->packagetype_atom = packagetype;
	t->identifier = calc_identifier(codename,component,architecture,packagetype);
	if( t->codename == NULL || t->identifier == NULL ) {
		(void)target_free(t);
		return RET_ERROR_OOM;
	}
	t->getversion = getversion;
	t->getinstalldata = getinstalldata;
	t->getarchitecture = getarchitecture;
	t->getfilekeys = getfilekeys;
	t->getchecksums = getchecksums;
	t->getsourceandversion = getsourceandversion;
	t->doreoverride = doreoverride;
	t->doretrack = doretrack;
	*d = t;
	return RET_OK;
}

retvalue target_initialize_ubinary(const char *codename, component_t component, architecture_t architecture, const struct exportmode *exportmode, struct target **target) {
	return target_initialize(codename, component, architecture, pt_udeb,
			binaries_getversion,
			binaries_getinstalldata,
			binaries_getarchitecture,
			binaries_getfilekeys, binaries_getchecksums,
			binaries_getsourceandversion,
			ubinaries_doreoverride, binaries_retrack,
			mprintf("%s/debian-installer/binary-%s",
				atoms_components[component],
				atoms_architectures[architecture]),
			exportmode, target);
}
retvalue target_initialize_binary(const char *codename, component_t component, architecture_t architecture, const struct exportmode *exportmode, struct target **target) {
	return target_initialize(codename, component, architecture, pt_deb,
			binaries_getversion,
			binaries_getinstalldata,
			binaries_getarchitecture,
			binaries_getfilekeys, binaries_getchecksums,
			binaries_getsourceandversion,
			binaries_doreoverride, binaries_retrack,
			mprintf("%s/binary-%s",
				atoms_components[component],
				atoms_architectures[architecture]),
			exportmode, target);
}

retvalue target_initialize_source(const char *codename, component_t component, const struct exportmode *exportmode, struct target **target) {
	return target_initialize(codename, component, architecture_source, pt_dsc,
			sources_getversion,
			sources_getinstalldata,
			sources_getarchitecture,
			sources_getfilekeys, sources_getchecksums,
			sources_getsourceandversion,
			sources_doreoverride, sources_retrack,
			mprintf("%s/source", atoms_components[component]),
			exportmode, target);
}

retvalue target_free(struct target *target) {
	retvalue result = RET_OK;

	if( target == NULL )
		return RET_OK;
	if( target->packages != NULL ) {
		result = target_closepackagesdb(target);
	} else
		result = RET_OK;
	if( target->wasmodified ) {
		fprintf(stderr,"Warning: database '%s' was modified but no index file was exported.\nChanges will only be visible after the next 'export'!\n",target->identifier);
	}

	free(target->codename);
	free(target->identifier);
	free(target->relativedirectory);
	free(target);
	return result;
}

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(struct target *target, struct database *database, bool readonly) {
	retvalue r;

	assert( target->packages == NULL );
	if( target->packages != NULL )
		return RET_OK;
	r = database_openpackages(database, target->identifier, readonly,
			&target->packages);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		target->packages = NULL;
		return r;
	}
	return r;
}

/* this closes databases... */
retvalue target_closepackagesdb(struct target *target) {
	retvalue r;

	if( target->packages == NULL ) {
		fprintf(stderr,"Internal Warning: Double close!\n");
		r = RET_OK;
	} else {
		r = table_close(target->packages);
		target->packages = NULL;
	}
	return r;
}

/* Remove a package from the given target. */
retvalue target_removereadpackage(struct target *target, struct logger *logger, struct database *database, const char *name, const char *oldcontrol, struct trackingdata *trackingdata) {
	char *oldpversion = NULL;
	struct strlist files;
	retvalue result,r;
	char *oldsource,*oldsversion;

	assert( target != NULL && target->packages != NULL );
	assert( oldcontrol != NULL && name != NULL );

	if( logger != NULL ) {
		/* need to get the version for logging, if not available */
		r = target->getversion(oldcontrol, &oldpversion);
		if( !RET_IS_OK(r) )
			oldpversion = NULL;
	}
	r = target->getfilekeys(oldcontrol, &files);
	if( RET_WAS_ERROR(r) ) {
		free(oldpversion);
		return r;
	}
	if( trackingdata != NULL ) {
		r = target->getsourceandversion(oldcontrol,
				name, &oldsource, &oldsversion);
		if( !RET_IS_OK(r) ) {
			oldsource = oldsversion = NULL;
		}
	} else {
		oldsource = oldsversion = NULL;
	}
	if( verbose > 0 )
		printf("removing '%s' from '%s'...\n",
				name, target->identifier);
	result = table_deleterecord(target->packages, name, false);
	if( RET_IS_OK(result) ) {
		target->wasmodified = true;
		if( oldsource!= NULL && oldsversion != NULL ) {
			r = trackingdata_remove(trackingdata,
					oldsource, oldsversion, &files);
			RET_UPDATE(result,r);
		}
		if( logger != NULL )
			logger_log(logger, target, name,
					NULL, oldpversion,
					NULL, oldcontrol,
					NULL, &files);
		r = references_delete(database, target->identifier, &files,
				NULL);
		RET_UPDATE(result, r);
	}
	strlist_done(&files);
	free(oldpversion);
	return result;
}

/* Remove a package from the given target. */
retvalue target_removepackage(struct target *target, struct logger *logger, struct database *database, const char *name, struct trackingdata *trackingdata) {
	char *oldchunk;
	retvalue r;

	assert(target != NULL && target->packages != NULL && name != NULL );

	r = table_getrecord(target->packages, name, &oldchunk);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	else if( r == RET_NOTHING ) {
		if( verbose >= 10 )
			fprintf(stderr,"Could not find '%s' in '%s'...\n",
					name, target->identifier);
		return RET_NOTHING;
	}
	r = target_removereadpackage(target, logger, database,
			name, oldchunk, trackingdata);
	free(oldchunk);
	return r;
}


/* Like target_removepackage, but delete the package record by cursor */
retvalue target_removepackage_by_cursor(struct target_cursor *tc, struct logger *logger, struct database *database, struct trackingdata *trackingdata) {
	struct target * const target = tc->target;
	const char * const name = tc->lastname;
	const char * const control = tc->lastcontrol;
	char *oldpversion = NULL;
	struct strlist files;
	retvalue result, r;
	char *oldsource, *oldsversion;

	assert( target != NULL && target->packages != NULL );
	assert( name != NULL && control != NULL );

	if( logger != NULL ) {
		/* need to get the version for logging, if not available */
		r = target->getversion(control, &oldpversion);
		if( !RET_IS_OK(r) )
			oldpversion = NULL;
	}
	r = target->getfilekeys(control, &files);
	if( RET_WAS_ERROR(r) ) {
		free(oldpversion);
		return r;
	}
	if( trackingdata != NULL ) {
		r = target->getsourceandversion(control,
				name, &oldsource, &oldsversion);
		if( !RET_IS_OK(r) ) {
			oldsource = oldsversion = NULL;
		}
	} else {
		oldsource = oldsversion = NULL;
	}
	if( verbose > 0 )
		printf("removing '%s' from '%s'...\n",
				name, target->identifier);
	result = cursor_delete(target->packages, tc->cursor, tc->lastname, NULL);
	if( RET_IS_OK(result) ) {
		target->wasmodified = true;
		if( oldsource != NULL && oldsversion != NULL ) {
			r = trackingdata_remove(trackingdata,
					oldsource, oldsversion, &files);
			RET_UPDATE(result,r);
		}
		if( logger != NULL )
			logger_log(logger, target, name,
					NULL, oldpversion,
					NULL, control,
					NULL, &files);
		r = references_delete(database, target->identifier, &files,
				NULL);
		RET_UPDATE(result, r);
	}
	strlist_done(&files);
	free(oldpversion);
	return result;
}

static retvalue addpackages(struct target *target, struct database *database,
		const char *packagename, const char *controlchunk,
		/*@null@*/const char *oldcontrolchunk,
		const char *version, /*@null@*/const char *oldversion,
		const struct strlist *files,
		/*@only@*//*@null@*/struct strlist *oldfiles,
		/*@null@*/struct logger *logger,
		/*@null@*/struct trackingdata *trackingdata,
		architecture_t architecture,
		/*@null@*//*@only@*/char *oldsource,/*@null@*//*@only@*/char *oldsversion) {

	retvalue result,r;
	struct table *table = target->packages;
	enum filetype filetype;

	assert( atom_defined(architecture) );

	if( architecture == architecture_source )
		filetype = ft_SOURCE;
	else if( architecture == architecture_all )
		filetype = ft_ALL_BINARY;
	else
		filetype = ft_ARCH_BINARY;

	/* mark it as needed by this distribution */

	r = references_insert(database, target->identifier,
			files, oldfiles);

	if( RET_WAS_ERROR(r) ) {
		if( oldfiles != NULL )
			strlist_done(oldfiles);
		return r;
	}

	/* Add package to the distribution's database */

	if( oldcontrolchunk != NULL ) {
		result = table_replacerecord(table, packagename, controlchunk);

	} else {
		result = table_adduniqrecord(table, packagename, controlchunk);
	}

	if( RET_WAS_ERROR(result) ) {
		if( oldfiles != NULL )
			strlist_done(oldfiles);
		return result;
	}

	if( logger != NULL )
		logger_log(logger, target, packagename,
				version, oldversion,
				controlchunk, oldcontrolchunk,
				files, oldfiles);

	r = trackingdata_insert(trackingdata, filetype, files,
			oldsource, oldsversion, oldfiles, database);
	RET_UPDATE(result,r);

	/* remove old references to files */

	if( oldfiles != NULL ) {
		r = references_delete(database, target->identifier,
				oldfiles, files);
		RET_UPDATE(result,r);
		strlist_done(oldfiles);
	}

	return result;
}

retvalue target_addpackage(struct target *target, struct logger *logger, struct database *database, const char *name, const char *version, const char *control, const struct strlist *filekeys, bool *usedmarker, bool downgrade, struct trackingdata *trackingdata, enum filetype filetype) {
	struct strlist oldfilekeys,*ofk;
	char *oldcontrol,*oldsource,*oldsversion;
	char *oldpversion;
	retvalue r;

	assert(target->packages!=NULL);

	r = table_getrecord(target->packages, name, &oldcontrol);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		ofk = NULL;
		oldsource = NULL;
		oldsversion = NULL;
		oldpversion = NULL;
		oldcontrol = NULL;
	} else {

		r = target->getversion(oldcontrol, &oldpversion);
		if( RET_WAS_ERROR(r) && !IGNORING_(brokenold,"Error parsing old version!\n") ) {
			free(oldcontrol);
			return r;
		}
		if( RET_IS_OK(r) ) {
			int versioncmp;

			r = dpkgversions_cmp(version,oldpversion,&versioncmp);
			if( RET_WAS_ERROR(r) ) {
				if( !IGNORING_(brokenversioncmp,"Parse errors processing versions of %s.\n",name) ) {
					free(oldpversion);
					free(oldcontrol);
					return r;
				}
			} else {
				if( versioncmp <= 0 ) {
					/* new Version is not newer than old version */
					if( downgrade ) {
						fprintf(stderr,"Warning: downgrading '%s' from '%s' to '%s' in '%s'!\n",name,oldpversion,version,target->identifier);
					} else {
						fprintf(stderr,"Skipping inclusion of '%s' '%s' in '%s', as it has already '%s'.\n",name,version,target->identifier,oldpversion);
						free(oldpversion);
						free(oldcontrol);
						return RET_NOTHING;
					}
				}
			}
		} else
			oldpversion = NULL;
		r = target->getfilekeys(oldcontrol, &oldfilekeys);
		ofk = &oldfilekeys;
		if( RET_WAS_ERROR(r) ) {
			if( IGNORING_(brokenold,"Error parsing files belonging to installed version of %s!\n",name)) {
				ofk = NULL;
				oldsversion = oldsource = NULL;
			} else {
				free(oldcontrol);
				free(oldpversion);
				return r;
			}
		} else if(trackingdata != NULL) {
			r = target->getsourceandversion(oldcontrol,
					name, &oldsource, &oldsversion);
			if( RET_WAS_ERROR(r) ) {
				strlist_done(ofk);
				if( IGNORING_(brokenold,"Error searching for source name of installed version of %s!\n",name)) {
					// TODO: free something of oldfilekeys?
					ofk = NULL;
					oldsversion = oldsource = NULL;
				} else {
					free(oldcontrol);
					free(oldpversion);
					return r;
				}
			}
		} else {
			oldsversion = oldsource = NULL;
		}

	}
	if( usedmarker != NULL )
		*usedmarker = true;
	r = addpackages(target, database, name, control, oldcontrol,
			version, oldpversion,
			filekeys, ofk,
			logger,
			trackingdata, filetype, oldsource, oldsversion);
	if( RET_IS_OK(r) ) {
		target->wasmodified = true;
	}
	free(oldpversion);
	free(oldcontrol);

	return r;
}

retvalue target_checkaddpackage(struct target *target, const char *name, const char *version, bool tracking, bool permitnewerold) {
	struct strlist oldfilekeys,*ofk;
	char *oldcontrol,*oldsource,*oldsversion;
	char *oldpversion;
	retvalue r;

	assert(target->packages!=NULL);

	r = table_getrecord(target->packages, name, &oldcontrol);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		ofk = NULL;
		oldsource = NULL;
		oldsversion = NULL;
		oldpversion = NULL;
		oldcontrol = NULL;
	} else {
		int versioncmp;

		r = target->getversion(oldcontrol, &oldpversion);
		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr,
"Error extracting version from old '%s' in '%s'. Database corrupted?\n", name, target->identifier);
			free(oldcontrol);
			return r;
		}
		assert( RET_IS_OK(r) );

		r = dpkgversions_cmp(version,oldpversion,&versioncmp);
		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr,
"Parse error comparing version '%s' of '%s' with old version '%s' in '%s'\n.",
					version, name, oldpversion,
					target->identifier);
			free(oldpversion);
			free(oldcontrol);
			return r;
		}
		if( versioncmp <= 0 ) {
			r = RET_NOTHING;
			if( versioncmp < 0 ) {
				if( !permitnewerold ) {
					fprintf(stderr,
"Error: trying to put version '%s' of '%s' in '%s',\n"
"while there already is the stricly newer '%s' in there.\n"
"(To ignore this error add Permit: older_version.)\n"
						,name, version,
						target->identifier,
						oldpversion);
					r = RET_ERROR;
				} else if( verbose >= 0 ) {
					printf(
"Warning: trying to put version '%s' of '%s' in '%s',\n"
"while there already is '%s' in there.\n",
						name, version,
						target->identifier,
						oldpversion);
				}
			} else if( verbose > 2 ) {
					printf(
"Will not put '%s' in '%s', as already there with same version '%s'.\n",
						name, target->identifier,
						oldpversion);

			}
			free(oldpversion);
			free(oldcontrol);
			return r;
		}
		r = target->getfilekeys(oldcontrol, &oldfilekeys);
		ofk = &oldfilekeys;
		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr,
"Error extracting installed files from old '%s' in '%s'.\nDatabase corrupted?\n",
				name, target->identifier);
			free(oldcontrol);
			free(oldpversion);
			return r;
		}
		if( tracking ) {
			r = target->getsourceandversion(oldcontrol,
					name, &oldsource, &oldsversion);
			if( RET_WAS_ERROR(r) ) {
				fprintf(stderr,
"Error extracting source name and version from '%s' in '%s'. Database corrupted?\n",
						name, target->identifier);
				strlist_done(ofk);
				free(oldcontrol);
				free(oldpversion);
				return r;
			}
			/* TODO: check if tracking would succeed */
			free(oldsversion);
			free(oldsource);
		}
		strlist_done(ofk);
	}
	free(oldpversion);
	free(oldcontrol);
	return RET_OK;
}

retvalue target_rereference(struct target *target, struct database *database) {
	retvalue result,r;
	struct target_cursor iterator;
	const char *package, *control;

	if( verbose > 1 ) {
		if( verbose > 2 )
			printf("Unlocking depencies of %s...\n",
					target->identifier);
		else
			printf("Rereferencing %s...\n",
					target->identifier);
	}

	result = references_remove(database, target->identifier);
	if( verbose > 2 )
		printf("Referencing %s...\n", target->identifier);

	r = target_openiterator(target, database, READONLY, &iterator);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;
	while( target_nextpackage(&iterator, &package, &control) ) {
		struct strlist filekeys;

		r = target->getfilekeys(control, &filekeys);
		RET_UPDATE(result, r);
		if( !RET_IS_OK(r) )
			continue;
		if( verbose > 10 ) {
			fprintf(stderr, "adding references to '%s' for '%s': ",
					target->identifier, package);
			(void)strlist_fprint(stderr, &filekeys);
			(void)putc('\n', stderr);
		}
		r = references_insert(database, target->identifier, &filekeys, NULL);
		strlist_done(&filekeys);
		RET_UPDATE(result, r);
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue package_referenceforsnapshot(struct database *database, UNUSED(struct distribution *di), struct target *target, const char *package, const char *chunk, void *data) {
	const char *identifier = data;
	struct strlist filekeys;
	retvalue r;

	r = target->getfilekeys(chunk, &filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 15 ) {
		fprintf(stderr, "adding references to '%s' for '%s': ",
				identifier, package);
		(void)strlist_fprint(stderr, &filekeys);
		(void)putc('\n', stderr);
	}
	r = references_add(database, identifier, &filekeys);
	strlist_done(&filekeys);
	return r;
}

retvalue package_check(struct database *database, UNUSED(struct distribution *di), struct target *target, const char *package, const char *chunk, UNUSED(void *pd)) {
	struct checksumsarray files;
	struct strlist expectedfilekeys;
	char *dummy, *version;
	retvalue result,r;
	architecture_t package_architecture;

	r = target->getversion(chunk, &version);
	if( !RET_IS_OK(r) ) {
		fprintf(stderr, "Error extraction version number from package control info of '%s'!\n", package);
		if( r == RET_NOTHING )
			r = RET_ERROR_MISSING;
		return r;
	}
	r = target->getarchitecture(chunk, &package_architecture);
	if( !RET_IS_OK(r) ) {
		fprintf(stderr, "Error extraction architecture from package control info of '%s'!\n", package);
		if( r == RET_NOTHING )
			r = RET_ERROR_MISSING;
		return r;
	}
	r = target->getinstalldata(target, package, version,
			package_architecture, chunk, &dummy,
			&expectedfilekeys, &files);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr, "Error extracting information of package '%s'!\n",
				package);
	}
	free(version);
	result = r;
	if( RET_IS_OK(r) ) {
		free(dummy);
		if( !strlist_subset(&expectedfilekeys, &files.names, NULL) ||
		    !strlist_subset(&expectedfilekeys, &files.names, NULL) ) {
			(void)fprintf(stderr, "Reparsing the package information of '%s' yields to the expectation to find:\n", package);
			(void)strlist_fprint(stderr, &expectedfilekeys);
			(void)fputs("but found:\n", stderr);
			(void)strlist_fprint(stderr, &files.names);
			(void)putc('\n',stderr);
			result = RET_ERROR;
		}
		strlist_done(&expectedfilekeys);
	} else {
		r = target->getchecksums(chunk, &files);
		if( r == RET_NOTHING )
			r = RET_ERROR;
		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr, "Even more errors extracting information of package '%s'!\n",
					package);
			return r;
		}
	}

	if( verbose > 10 ) {
		fprintf(stderr, "checking files of '%s'\n", package);
	}
	r = files_expectfiles(database, &files.names, files.checksums);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Files are missing for '%s'!\n", package);
	}
	RET_UPDATE(result,r);
	if( verbose > 10 ) {
		(void)fprintf(stderr, "checking references to '%s' for '%s': ",
				target->identifier, package);
		(void)strlist_fprint(stderr, &files.names);
		(void)putc('\n', stderr);
	}
	r = references_check(database, target->identifier, &files.names);
	RET_UPDATE(result,r);
	checksumsarray_done(&files);
	return result;
}

/* Reapply override information */

retvalue target_reoverride(struct target *target, struct distribution *distribution, struct database *database) {
	struct target_cursor iterator;
	retvalue result, r;
	const char *package, *controlchunk;

	assert(target->packages == NULL);
	assert(distribution != NULL);

	if( verbose > 1 ) {
		fprintf(stderr,"Reapplying overrides packages in '%s'...\n",target->identifier);
	}

	r = target_openiterator(target, database, READWRITE, &iterator);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( target_nextpackage(&iterator, &package, &controlchunk) ) {
		char *newcontrolchunk = NULL;

		r = target->doreoverride(distribution, package, controlchunk,
				&newcontrolchunk);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) ) {
			if( verbose > 0 )
				(void)fputs("target_reoverride: Stopping procession of further packages due to previous errors\n", stderr);
			break;
		}
		if( RET_IS_OK(r) ) {
			r = cursor_replace(target->packages, iterator.cursor,
				newcontrolchunk, strlen(newcontrolchunk));
			free(newcontrolchunk);
			if( RET_WAS_ERROR(r) ) {
				result = r;
				break;
			}
			target->wasmodified = true;
		}
	}
	r = target_closeiterator(&iterator);
	RET_ENDUPDATE(result, r);
	return result;
}

/* export a database */

retvalue target_export(struct target *target, struct database *database, bool onlyneeded, bool snapshot, struct release *release) {
	retvalue result;
	bool onlymissing;

	if( verbose > 5 ) {
		if( onlyneeded )
			printf(" looking for changes in '%s'...\n",target->identifier);
		else
			printf(" exporting '%s'...\n",target->identifier);
	}

	/* not exporting if file is already there? */
	onlymissing = onlyneeded && !target->wasmodified;

	result = export_target(target->relativedirectory,
				target, database,
				target->exportmode,
				release,
				onlymissing, snapshot);

	if( !RET_WAS_ERROR(result) && !snapshot ) {
		target->saved_wasmodified =
			target->saved_wasmodified || target->wasmodified;
		target->wasmodified = false;
	}
	return result;
}

retvalue package_rerunnotifiers(UNUSED(struct database *da), struct distribution *distribution, struct target *target, const char *package, const char *chunk, UNUSED(void *data)) {
	struct logger *logger = distribution->logger;
	struct strlist filekeys;
	char *version;
	retvalue r;

	r = target->getversion(chunk, &version);
	if( !RET_IS_OK(r) ) {
		fprintf(stderr,"Error extraction version number from package control info of '%s'!\n",package);
		if( r == RET_NOTHING )
			r = RET_ERROR_MISSING;
		return r;
	}
	r = target->getfilekeys(chunk, &filekeys);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error extracting information about used files from package '%s'!\n",package);
		free(version);
		return r;
	}
	r = logger_reruninfo(logger, target, package, version, chunk, &filekeys);
	strlist_done(&filekeys);
	free(version);
	return r;
}
