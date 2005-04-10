/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include "override.h"
#include "target.h"

extern int verbose;

static retvalue target_initialize(
	const char *codename,const char *component,const char *architecture,
	/*@observer@*/const char *packagetype,
	get_name getname,get_version getversion,get_installdata getinstalldata,
	get_filekeys getfilekeys, get_upstreamindex getupstreamindex,
	do_reoverride doreoverride,
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
	t->doreoverride = doreoverride;
	*d = t;
	return RET_OK;
}

retvalue target_initialize_ubinary(const char *codename,const char *component,const char *architecture,const struct exportmode *exportmode,struct target **target) {
	return target_initialize(codename,component,architecture,"udeb",binaries_getname,binaries_getversion,binaries_getinstalldata,binaries_getfilekeys,ubinaries_getupstreamindex,ubinaries_doreoverride,mprintf("%s/debian-installer/binary-%s",component,architecture),exportmode,target);
}
retvalue target_initialize_binary(const char *codename,const char *component,const char *architecture,const struct exportmode *exportmode,struct target **target) {
	return target_initialize(codename,component,architecture,"deb",binaries_getname,binaries_getversion,binaries_getinstalldata,binaries_getfilekeys,binaries_getupstreamindex,binaries_doreoverride,mprintf("%s/binary-%s",component,architecture),exportmode,target);
}

retvalue target_initialize_source(const char *codename,const char *component,const struct exportmode *exportmode,struct target **target) {
	return target_initialize(codename,component,"source","dsc",sources_getname,sources_getversion,sources_getinstalldata,sources_getfilekeys,sources_getupstreamindex,sources_doreoverride,mprintf("%s/source",component),exportmode,target);
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
retvalue target_initpackagesdb(struct target *target, const char *dbdir) {
	retvalue r;

	if( target->packages != NULL ) {
		fprintf(stderr,"again2\n");
		r = RET_OK;
	} else {
		r = packages_initialize(&target->packages,dbdir,target->identifier);
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
retvalue target_removepackage(struct target *target,references refs,const char *name, struct strlist *dereferencedfilekeys) {
	char *oldchunk;
	struct strlist files;
	retvalue r;

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
	r = target->getfilekeys(target,oldchunk,&files,NULL);
	free(oldchunk);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	if( verbose > 0 )
		fprintf(stderr,"removing '%s' from '%s'...\n",name,target->identifier);
	r = packages_remove(target->packages,name);
	if( RET_IS_OK(r) ) {
		target->wasmodified = TRUE;
		r = references_delete(refs,target->identifier,&files,NULL,dereferencedfilekeys);
	} else
		strlist_done(&files);
	return r;
}

retvalue target_addpackage(struct target *target,references refs,const char *name,const char *version,const char *control,const struct strlist *filekeys,int force,bool_t downgrade, struct strlist *dereferencedfilekeys) {
	struct strlist oldfilekeys,*ofk;
	char *oldcontrol;
	retvalue r;

	assert(target->packages!=NULL);

	r = packages_get(target->packages,name,&oldcontrol);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING )
		ofk = NULL;
	else {
		char *oldversion;

		r = target->getversion(target,oldcontrol,&oldversion);
		if( RET_WAS_ERROR(r) && force <= 0) {
			free(oldcontrol);
			return r;
		}
		if( RET_IS_OK(r) ) {
			int versioncmp;

			r = dpkgversions_cmp(version,oldversion,&versioncmp);
			if( RET_WAS_ERROR(r) ) {
				fprintf(stderr,"Parse errors processing versions of %s.\n",name);
				if( force <= 0 ) {
					free(oldversion);
					free(oldcontrol);
					return r;
				}
			} else {
				if( versioncmp <= 0 ) {
					/* new Version is not newer than old version */
					if( downgrade ) {
						fprintf(stderr,"Warning: downgrading '%s' from '%s' to '%s' in '%s'!\n",name,oldversion,version,target->identifier);
					} else {
						fprintf(stderr,"Skipping inclusion of '%s' '%s' in '%s', as it has already '%s'.\n",name,version,target->identifier,oldversion);
						free(oldversion);
						free(oldcontrol);
						return RET_NOTHING;
					}
				}
			}
			free(oldversion);
		}

		r = (*target->getfilekeys)(target,oldcontrol,&oldfilekeys,NULL);
		free(oldcontrol);
		ofk = &oldfilekeys;
		if( RET_WAS_ERROR(r) ) {
			if( force > 0 )
				ofk = NULL;
			else
				return r;
		}
	}
	r = packages_insert(refs,target->packages,name,control,filekeys,ofk,dereferencedfilekeys);
	if( RET_IS_OK(r) )
		target->wasmodified = TRUE;

	return r;
}

/* rereference a full database */
struct data_reref { 
	/*@dependent@*/references refs;
	/*@dependent@*/struct target *target;
};

static retvalue rereferencepkg(void *data,const char *package,const char *chunk) {
	struct data_reref *d = data;
	struct strlist filekeys;
	retvalue r;

	r = (*d->target->getfilekeys)(d->target,chunk,&filekeys,NULL);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 10 ) {
		fprintf(stderr,"adding references to '%s' for '%s': ",d->target->identifier,package);
		(void)strlist_fprint(stderr,&filekeys);
		(void)putc('\n',stderr);
	}
	r = references_insert(d->refs,d->target->identifier,&filekeys,NULL);
	strlist_done(&filekeys);
	return r;
}

retvalue target_rereference(struct target *target,references refs,int force) {
	retvalue result,r;
	struct data_reref refdata;

	assert(target->packages!=NULL);

	if( verbose > 1 ) {
		if( verbose > 2 )
			fprintf(stderr,"Unlocking depencies of %s...\n",target->identifier);
		else
			fprintf(stderr,"Rereferencing %s...\n",target->identifier);
	}

	result = references_remove(refs,target->identifier);

	if( verbose > 2 )
		fprintf(stderr,"Referencing %s...\n",target->identifier);

	refdata.refs = refs;
	refdata.target = target;
	r = packages_foreach(target->packages,rereferencepkg,&refdata,force);
	RET_UPDATE(result,r);
	
	return result;
}

/* check a full database */
struct data_check { 
	/*@dependent@*/references refs;
	/*@dependent@*/filesdb filesdb;
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
	r = files_expectfiles(d->filesdb,&actualfilekeys,&md5sums);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Files are missing for '%s'!\n",package);
	}
	RET_UPDATE(result,r);
	if( verbose > 10 ) {
		(void)fprintf(stderr,"checking references to '%s' for '%s': ",d->target->identifier,package);
		(void)strlist_fprint(stderr,&actualfilekeys);
		(void)putc('\n',stderr);
	}
	r = references_check(d->refs,d->target->identifier,&actualfilekeys);
	RET_UPDATE(result,r);
	strlist_done(&actualfilekeys);
	strlist_done(&md5sums);
	return result;
}

retvalue target_check(struct target *target,filesdb filesdb,references refs,int force) {
	struct data_check data;

	assert(target->packages!=NULL);
	if( verbose > 1 ) {
		fprintf(stderr,"Checking packages in '%s'...\n",target->identifier);
	}
	data.refs = refs;
	data.filesdb = filesdb;
	data.target = target;
	return packages_foreach(target->packages,checkpkg,&data,force);
}

/* Reapply override information */

retvalue target_reoverride(struct target *target,const struct alloverrides *ao) {
	assert(target->packages!=NULL);
	assert(ao!=NULL);

	if( verbose > 1 ) {
		fprintf(stderr,"Reapplying overrides packages in '%s'...\n",target->identifier);
	}
	return packages_modifyall(target->packages,target->doreoverride,(void*)ao,&target->wasmodified);
}

/* export a database */

retvalue target_export(struct target *target,const char *confdir,const char *dbdir,const char *dirofdist,int force,bool_t onlyneeded, struct strlist *releasedfiles ) {
	retvalue result,r;
	bool_t onlymissing;

	if( verbose > 5 ) {
		if( onlyneeded )
			fprintf(stderr," looking for changes in '%s'...\n",target->identifier);
		else
			fprintf(stderr," exporting '%s'...\n",target->identifier);
	}

	r = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;

	/* not exporting if file is already there? */
	onlymissing = onlyneeded && !target->wasmodified;

	result = export_target(confdir,dirofdist,target->relativedirectory,
				target->packages,
				target->exportmode,
				releasedfiles,
				onlymissing, force);
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);

	if( !RET_WAS_ERROR(result) ) {
		target->wasmodified = FALSE;
	}
	return result;
}

retvalue target_mkdistdir(struct target *target,const char *distdir) {
	char *dirname;retvalue r;

	dirname = calc_dirconcat3(distdir,target->codename,target->relativedirectory);
	if( dirname == NULL )
		return RET_ERROR_OOM;
	r = dirs_make_recursive(dirname);
	free(dirname);
	return r;
}
