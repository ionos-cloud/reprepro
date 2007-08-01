/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007 Bernhard R. Link
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
#include <malloc.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "packages.h"
#include "reference.h"
#include "binaries.h"
#include "sources.h"
#include "names.h"
#include "md5sum.h"
#include "dirs.h"
#include "dpkgversions.h"
#include "tracking.h"
#include "log.h"
#include "files.h"
#include "target.h"

extern int verbose;

static retvalue target_initialize(
	const char *codename,const char *component,const char *architecture,
	/*@observer@*/const char *packagetype,
	get_name getname,get_version getversion,get_installdata getinstalldata,
	get_filekeys getfilekeys, get_upstreamindex getupstreamindex,
	get_sourceandversion getsourceandversion,
	do_reoverride doreoverride,do_retrack doretrack,
	/*@null@*//*@only@*/char *directory, /*@dependent@*/const struct exportmode *exportmode, /*@out@*/struct target **d) {

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
	t->component = strdup(component);
	t->architecture = strdup(architecture);
	t->packagetype = packagetype;
	t->identifier = calc_identifier(codename,component,architecture,packagetype);
	if( t->codename == NULL || t->component == NULL ||
			t->architecture == NULL || t->identifier == NULL ) {
		(void)target_free(t);
		return RET_ERROR_OOM;
	}
	t->getname = getname;
	t->getversion = getversion;
	t->getinstalldata = getinstalldata;
	t->getfilekeys = getfilekeys;
	t->getupstreamindex = getupstreamindex;
	t->getsourceandversion = getsourceandversion;
	t->doreoverride = doreoverride;
	t->doretrack = doretrack;
	*d = t;
	return RET_OK;
}

retvalue target_initialize_ubinary(const char *codename,const char *component,const char *architecture,const struct exportmode *exportmode,struct target **target) {
	return target_initialize(codename,component,architecture,"udeb",binaries_getname,binaries_getversion,binaries_getinstalldata,binaries_getfilekeys,ubinaries_getupstreamindex,binaries_getsourceandversion,ubinaries_doreoverride,binaries_retrack,mprintf("%s/debian-installer/binary-%s",component,architecture),exportmode,target);
}
retvalue target_initialize_binary(const char *codename,const char *component,const char *architecture,const struct exportmode *exportmode,struct target **target) {
	return target_initialize(codename,component,architecture,"deb",binaries_getname,binaries_getversion,binaries_getinstalldata,binaries_getfilekeys,binaries_getupstreamindex,binaries_getsourceandversion,binaries_doreoverride,binaries_retrack,mprintf("%s/binary-%s",component,architecture),exportmode,target);
}

retvalue target_initialize_source(const char *codename,const char *component,const struct exportmode *exportmode,struct target **target) {
	return target_initialize(codename,component,"source","dsc",sources_getname,sources_getversion,sources_getinstalldata,sources_getfilekeys,sources_getupstreamindex,sources_getsourceandversion,sources_doreoverride,sources_retrack,mprintf("%s/source",component),exportmode,target);
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
	free(target->component);
	free(target->architecture);
	free(target->identifier);
	free(target->relativedirectory);
	free(target);
	return result;
}

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(struct target *target, struct database *database) {
	retvalue r;

	if( target->packages != NULL ) {
		fprintf(stderr,"again2\n");
		r = RET_OK;
	} else {
		r = packages_initialize(&target->packages, database,
				target->identifier);
		if( RET_WAS_ERROR(r) ) {
			target->packages = NULL;
			return r;
		}
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
		r = packages_done(target->packages);
		target->packages = NULL;
	}
	return r;
}

/* Remove a package from the given target. If dereferencedfilekeys != NULL, add there the
 * filekeys that lost references */
retvalue target_removepackage(struct target *target,struct logger *logger,struct database *database,const char *name,const char *oldpversion,struct strlist *dereferencedfilekeys,struct trackingdata *trackingdata) {
	char *oldchunk,*oldpversion_ifunknown = NULL;
	struct strlist files;
	retvalue result,r;
	char *oldsource,*oldsversion;

	assert(target != NULL && target->packages != NULL && name != NULL );

	r = packages_get(target->packages,name,&oldchunk);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	else if( r == RET_NOTHING ) {
		if( verbose >= 10 )
			fprintf(stderr,"Could not find '%s' in '%s'...\n",
					name,target->identifier);
		return RET_NOTHING;
	}
	if( logger != NULL && oldpversion == NULL ) {
		/* need to get the version for logging, if not available */
		r = target->getversion(target,oldchunk,&oldpversion_ifunknown);
		if( RET_IS_OK(r) )
			oldpversion = oldpversion_ifunknown;
	}
	r = target->getfilekeys(target,oldchunk,&files,NULL);
	if( RET_WAS_ERROR(r) ) {
		free(oldpversion_ifunknown);
		free(oldchunk);
		return r;
	}
	if( trackingdata != NULL ) {
		r = (*target->getsourceandversion)(target, oldchunk,
				name, &oldsource, &oldsversion);
		if( !RET_IS_OK(r) ) {
			oldsource = oldsversion = NULL;
		}
	} else {
		oldsource = oldsversion = NULL;
	}
	if( verbose > 0 )
		printf("removing '%s' from '%s'...\n",name,target->identifier);
	result = packages_remove(target->packages,name);
	if( RET_IS_OK(result) ) {
		target->wasmodified = TRUE;
		if( oldsource!= NULL && oldsversion != NULL ) {
			r = trackingdata_remove(trackingdata,
					oldsource, oldsversion, &files);
			RET_UPDATE(result,r);
		}
		if( logger != NULL )
			logger_log(logger, target, name,
					NULL, oldpversion,
					NULL, oldchunk,
					NULL, &files);
		r = references_delete(database, target->identifier, &files,
				NULL, dereferencedfilekeys);
		RET_UPDATE(result, r);
	} else
		strlist_done(&files);
	free(oldpversion_ifunknown);
	free(oldchunk);
	return result;
}

static retvalue addpackages(struct target *target, struct database *database,
		const char *packagename, const char *controlchunk,
		/*@null@*/const char *oldcontrolchunk,
		const char *version, /*@null@*/const char *oldversion,
		const struct strlist *files,
		/*@only@*//*@null@*/struct strlist *oldfiles,
		/*@null@*/struct logger *logger,
		/*@null@*/struct strlist *dereferencedfilekeys,
		/*@null@*/struct trackingdata *trackingdata,
		enum filetype filetype,
		/*@null@*//*@only@*/char *oldsource,/*@null@*//*@only@*/char *oldsversion) {

	retvalue result,r;
	packagesdb packagesdb = target->packages;

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
		result = packages_replace(packagesdb,packagename,controlchunk);

	} else {
		result = packages_add(packagesdb,packagename,controlchunk);
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
				oldfiles, files, dereferencedfilekeys);
		RET_UPDATE(result,r);
	}

	return result;
}

retvalue target_addpackage(struct target *target,struct logger *logger,struct database *database,const char *name,const char *version,const char *control,const struct strlist *filekeys,bool_t downgrade, struct strlist *dereferencedfilekeys,struct trackingdata *trackingdata,enum filetype filetype) {
	struct strlist oldfilekeys,*ofk;
	char *oldcontrol,*oldsource,*oldsversion;
	char *oldpversion;
	retvalue r;

	assert(target->packages!=NULL);

	r = packages_get(target->packages,name,&oldcontrol);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		ofk = NULL;
		oldsource = NULL;
		oldsversion = NULL;
		oldpversion = NULL;
		oldcontrol = NULL;
	} else {

		r = target->getversion(target,oldcontrol,&oldpversion);
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
		r = (*target->getfilekeys)(target,oldcontrol,&oldfilekeys,NULL);
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
			r = (*target->getsourceandversion)(target,oldcontrol,name,&oldsource,&oldsversion);
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
	r = addpackages(target, database, name, control, oldcontrol,
			version, oldpversion,
			filekeys, ofk,
			logger,
			dereferencedfilekeys,
			trackingdata, filetype, oldsource, oldsversion);
	if( RET_IS_OK(r) ) {
		target->wasmodified = TRUE;
	}
	free(oldpversion);
	free(oldcontrol);

	return r;
}

retvalue target_checkaddpackage(struct target *target,const char *name,const char *version,bool_t tracking,bool_t permitnewerold) {
	struct strlist oldfilekeys,*ofk;
	char *oldcontrol,*oldsource,*oldsversion;
	char *oldpversion;
	retvalue r;

	assert(target->packages!=NULL);

	r = packages_get(target->packages,name,&oldcontrol);
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

		r = target->getversion(target,oldcontrol,&oldpversion);
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
		r = (*target->getfilekeys)(target,
				oldcontrol, &oldfilekeys, NULL);
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
			r = (*target->getsourceandversion)(target,
					oldcontrol, name, &oldsource, &oldsversion);
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

/* rereference a full database */
struct data_reref {
	/*@dependent@*/struct database *db;
	/*@dependent@*/struct target *target;
	/*@dependent@*/const char *identifier;
};

static retvalue rereferencepkg(void *data,const char *package,const char *chunk) {
	struct data_reref *d = data;
	struct strlist filekeys;
	retvalue r;

	r = (*d->target->getfilekeys)(d->target,chunk,&filekeys,NULL);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 10 ) {
		fprintf(stderr,"adding references to '%s' for '%s': ",d->identifier,package);
		(void)strlist_fprint(stderr,&filekeys);
		(void)putc('\n',stderr);
	}
	r = references_insert(d->db, d->identifier, &filekeys, NULL);
	strlist_done(&filekeys);
	return r;
}

retvalue target_rereference(struct target *target,struct database *database) {
	retvalue result,r;
	struct data_reref refdata;

	assert(target->packages!=NULL);

	if( verbose > 1 ) {
		if( verbose > 2 )
			printf("Unlocking depencies of %s...\n",target->identifier);
		else
			printf("Rereferencing %s...\n",target->identifier);
	}

	result = references_remove(database, target->identifier, NULL);

	if( verbose > 2 )
		printf("Referencing %s...\n",target->identifier);

	refdata.db = database;
	refdata.target = target;
	refdata.identifier = target->identifier;
	r = packages_foreach(target->packages,rereferencepkg,&refdata);
	RET_UPDATE(result,r);

	return result;
}

static retvalue snapshotreferencepkg(void *data,const char *package,const char *chunk) {
	struct data_reref *d = data;
	struct strlist filekeys;
	retvalue r;

	r = (*d->target->getfilekeys)(d->target,chunk,&filekeys,NULL);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 15 ) {
		fprintf(stderr,"adding references to '%s' for '%s': ",d->identifier,package);
		(void)strlist_fprint(stderr,&filekeys);
		(void)putc('\n',stderr);
	}
	r = references_add(d->db, d->identifier, &filekeys);
	strlist_done(&filekeys);
	return r;
}

retvalue target_addsnapshotreference(struct target *target,struct database *database,const char *name) {
	retvalue r,r2;
	struct data_reref refdata;
	char *id;

	assert(target->packages==NULL);

	id = mprintf("s=%s=%s", target->codename, name);
	if( id == NULL )
		return RET_ERROR_OOM;

	r = target_initpackagesdb(target, database);
	if( RET_WAS_ERROR(r) ) {
		free(id);
		return r;
	}

	if( verbose >= 1)
		fprintf(stderr,"Referencing snapshot '%s' of %s...\n",name,target->identifier);
	refdata.db = database;
	refdata.target = target;
	refdata.identifier = id;
	r = packages_foreach(target->packages,snapshotreferencepkg,&refdata);
	free(id);

	r2 = target_closepackagesdb(target);
	RET_ENDUPDATE(r,r2);

	return r;
}

/* retrack a full database */
struct data_retrack {
	/*@temp@*/trackingdb tracks;
	/*@temp@*/struct database *db;
	/*@temp@*/struct target *target;
};

static retvalue retrackpkg(void *data,const char *package,const char *chunk) {
	struct data_retrack *d = data;
	retvalue r;

	r = (*d->target->doretrack)(d->target,package,chunk,d->tracks,d->db);
	return r;
}

retvalue target_retrack(struct target *target,trackingdb tracks,struct database *database) {
	struct data_retrack trackdata;

	assert(target->packages!=NULL);

	if( verbose > 1 ) {
		fprintf(stderr,"  Tracking %s...\n",target->identifier);
	}

	trackdata.db = database;
	trackdata.tracks = tracks;
	trackdata.target = target;
	return packages_foreach(target->packages,retrackpkg,&trackdata);
}


/* check a full database */
struct data_check {
	/*@dependent@*/struct database *db;
	/*@dependent@*/struct target *target;
};

static retvalue checkpkg(void *data,const char *package,const char *chunk) {
	struct data_check *d = data;
	struct strlist expectedfilekeys,actualfilekeys,md5sums;
	char *dummy,*version;
	retvalue result,r;

	r = (*d->target->getversion)(d->target,chunk,&version);
	if( !RET_IS_OK(r) ) {
		fprintf(stderr,"Error extraction version number from package control info of '%s'!\n",package);
		if( r == RET_NOTHING )
			r = RET_ERROR_MISSING;
		return r;
	}
	r = (*d->target->getinstalldata)(d->target,package,version,chunk,&dummy,&expectedfilekeys,&md5sums,&actualfilekeys);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error extracting information of package '%s'!\n",package);
	}
	free(version);
	result = r;
	if( RET_IS_OK(r) ) {
		free(dummy);
		if( !strlist_subset(&expectedfilekeys,&actualfilekeys,NULL) ||
		    !strlist_subset(&expectedfilekeys,&actualfilekeys,NULL) ) {
			(void)fprintf(stderr,"Reparsing the package information of '%s' yields to the expectation to find:\n",package);
			(void)strlist_fprint(stderr,&expectedfilekeys);
			(void)fputs("but found:\n",stderr);
			(void)strlist_fprint(stderr,&actualfilekeys);
			(void)putc('\n',stderr);
			result = RET_ERROR;
		}
		strlist_done(&expectedfilekeys);
	} else {
		r = (*d->target->getfilekeys)(d->target,chunk,&actualfilekeys,&md5sums);
		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr,"Even more errors extracting information of package '%s'!\n",package);
			return r;
		}
	}

	if( verbose > 10 ) {
		fprintf(stderr,"checking files of '%s'\n",package);
	}
	r = files_expectfiles(d->db, &actualfilekeys, &md5sums);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Files are missing for '%s'!\n",package);
	}
	RET_UPDATE(result,r);
	if( verbose > 10 ) {
		(void)fprintf(stderr,"checking references to '%s' for '%s': ",d->target->identifier,package);
		(void)strlist_fprint(stderr,&actualfilekeys);
		(void)putc('\n',stderr);
	}
	r = references_check(d->db, d->target->identifier, &actualfilekeys);
	RET_UPDATE(result,r);
	strlist_done(&actualfilekeys);
	strlist_done(&md5sums);
	return result;
}

retvalue target_check(struct target *target,struct database *database) {
	struct data_check data;

	assert(target->packages!=NULL);
	if( verbose > 1 ) {
		printf("Checking packages in '%s'...\n",target->identifier);
	}
	data.db = database;
	data.target = target;
	return packages_foreach(target->packages,checkpkg,&data);
}

/* Reapply override information */

retvalue target_reoverride(struct target *target,const struct distribution *distribution) {
	assert(target->packages!=NULL);
	assert(distribution!=NULL);

	if( verbose > 1 ) {
		fprintf(stderr,"Reapplying overrides packages in '%s'...\n",target->identifier);
	}
	return packages_modifyall(target->packages,target->doreoverride,distribution,&target->wasmodified);
}

/* export a database */

retvalue target_export(struct target *target,const char *confdir,struct database *database,bool_t onlyneeded, bool_t snapshot, struct release *release) {
	retvalue result,r;
	bool_t onlymissing;

	if( verbose > 5 ) {
		if( onlyneeded )
			printf(" looking for changes in '%s'...\n",target->identifier);
		else
			printf(" exporting '%s'...\n",target->identifier);
	}

	r = target_initpackagesdb(target, database);
	if( RET_WAS_ERROR(r) )
		return r;

	/* not exporting if file is already there? */
	onlymissing = onlyneeded && !target->wasmodified;

	result = export_target(confdir,target->relativedirectory,
				target->packages,
				target->exportmode,
				release,
				onlymissing, snapshot);
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);

	if( !RET_WAS_ERROR(result) && !snapshot ) {
		target->saved_wasmodified =
			target->saved_wasmodified || target->wasmodified;
		target->wasmodified = FALSE;
	}
	return result;
}

/* call all log notificators again */
struct data_rerunnotify { struct target *target; struct logger *logger;};

static retvalue package_rerunnotify(void *data,const char *package,const char *chunk) {
	struct data_rerunnotify *d = data;
	struct strlist filekeys;
	char *version;
	retvalue r;

	r = (*d->target->getversion)(d->target, chunk, &version);
	if( !RET_IS_OK(r) ) {
		fprintf(stderr,"Error extraction version number from package control info of '%s'!\n",package);
		if( r == RET_NOTHING )
			r = RET_ERROR_MISSING;
		return r;
	}
	r = (*d->target->getfilekeys)(d->target, chunk, &filekeys, NULL);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error extracting information about used files from package '%s'!\n",package);
		free(version);
		return r;
	}
	r = logger_reruninfo(d->logger, d->target, package, version, chunk, &filekeys);
	strlist_done(&filekeys);
	free(version);
	return r;
}

retvalue target_rerunnotifiers(struct target *target, struct logger *logger) {
	struct data_rerunnotify data;

	assert(target->packages!=NULL);

	data.logger = logger;
	data.target = target;
	return packages_foreach(target->packages, package_rerunnotify, &data);
}
