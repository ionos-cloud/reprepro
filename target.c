/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2004 Bernhard R. Link
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
#include <assert.h>
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
#include "target.h"

extern int verbose;

static retvalue target_initialize(
	const char *codename,const char *component,const char *architecture,
	get_name getname,get_version getversion,get_installdata getinstalldata,
	get_filekeys getfilekeys, char *directory, target *d) {

	target t;

	if( directory == NULL )
		return RET_ERROR_OOM;

	t = calloc(1,sizeof(struct s_target));
	if( t == NULL ) {
		free(directory);
		return RET_ERROR_OOM;
	}
	t->directory = directory;
	t->codename = strdup(codename);
	t->component = strdup(component);
	t->architecture = strdup(architecture);
	t->identifier = calc_identifier(codename,component,architecture);
	if( !t->codename|| !t->component|| !t->architecture|| !t->identifier) {
		target_done(t);
		return RET_ERROR_OOM;
	}
	t->getname = getname;
	t->getversion = getversion;
	t->getinstalldata = getinstalldata;
	t->getfilekeys = getfilekeys;
	*d = t;
	return RET_OK;
}

retvalue target_initialize_binary(const char *codename,const char *component,const char *architecture,target *target) {
	return target_initialize(codename,component,architecture,binaries_getname,binaries_getversion,binaries_getinstalldata,binaries_getfilekeys,mprintf("%s/%s/binary-%s",codename,component,architecture),target);
}

retvalue target_initialize_source(const char *codename,const char *component,target *target) {
	return target_initialize(codename,component,"source",sources_getname,sources_getversion,sources_getinstalldata,sources_getfilekeys,mprintf("%s/%s/source",codename,component),target);
}


void target_done(target target) {
	if( target == NULL )
		return;
	free(target->codename);
	free(target->component);
	free(target->architecture);
	free(target->identifier);
	free(target->directory);
	free(target);
}

retvalue target_addpackage(target target,packagesdb packages,DB *references,filesdb files,const char *name,const char *version,const char *control,const struct strlist *filekeys,const struct strlist *md5sums,int force) {
	struct strlist oldfilekeys,*ofk;
	char *oldcontrol;
	retvalue r;

	r = files_expectfiles(files,filekeys,md5sums);
	if( RET_WAS_ERROR(r) )
		return r;
	r = packages_get(packages,name,&oldcontrol);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING )
		ofk = NULL;
	else {
		r = target->getfilekeys(target,name,oldcontrol,&oldfilekeys);
		free(oldcontrol);
		ofk = &oldfilekeys;
		if( RET_WAS_ERROR(r) ) {
			if( force )
				ofk = NULL;
			else
				return r;
		}
	}
	r = packages_insert(references,packages,name,control,filekeys,&oldfilekeys);
	if( ofk )
		strlist_done(ofk);
	return r;
}

/* rereference a full database */
struct data_reref { 
	packagesdb packagesdb;
	DB *referencesdb;
	target target;
};

static retvalue rereferencepkg(void *data,const char *package,const char *chunk) {
	struct data_reref *d = data;
	struct strlist filekeys;
	retvalue r;

	r = (*d->target->getfilekeys)(d->target,package,chunk,&filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 10 ) {
		fprintf(stderr,"adding references to '%s' for '%s': ",d->target->identifier,package);
		strlist_fprint(stderr,&filekeys);
		putc('\n',stderr);
	}
	r = references_insert(d->referencesdb,d->target->identifier,&filekeys,NULL);
	strlist_done(&filekeys);
	return r;
}

retvalue target_rereference(const char *dbdir,DB *referencesdb,target target,int force) {
	retvalue result,r;
	struct data_reref refdata;
	packagesdb pkgs;

	r = packages_initialize(&pkgs,dbdir,target->identifier);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 1 ) {
		if( verbose > 2 )
			fprintf(stderr,"Unlocking depencies of %s...\n",target->identifier);
		else
			fprintf(stderr,"Rereferencing %s...\n",target->identifier);
	}

	result = references_remove(referencesdb,target->identifier);

	if( verbose > 2 )
		fprintf(stderr,"Referencing %s...\n",target->identifier);

	refdata.referencesdb = referencesdb;
	refdata.packagesdb = pkgs;
	refdata.target = target;
	r = packages_foreach(pkgs,rereferencepkg,&refdata,force);
	RET_UPDATE(result,r);
	
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	return result;
}

/* check a full database */
struct data_check { 
	packagesdb packagesdb;
	DB *referencesdb;
	filesdb filesdb;
	target target;
};

static retvalue checkpkg(void *data,const char *package,const char *chunk) {
	struct data_check *d = data;
	struct strlist filekeys;
	retvalue r;

	r = (*d->target->getfilekeys)(d->target,package,chunk,&filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 10 ) {
		fprintf(stderr,"checking references to '%s' for '%s': ",d->target->identifier,package);
		strlist_fprint(stderr,&filekeys);
		putc('\n',stderr);
	}
	r = references_check(d->referencesdb,d->target->identifier,&filekeys);
	// TODO check md5sums in filesdb
	strlist_done(&filekeys);
	return r;
}

retvalue target_check(const char *dbdir,filesdb filesdb,DB *referencesdb,target target,int force) {
	retvalue result,r;
	struct data_check data;
	packagesdb pkgs;

	r = packages_initialize(&pkgs,dbdir,target->identifier);
	if( RET_WAS_ERROR(r) )
		return r;
	if( verbose > 1 ) {
		fprintf(stderr,"Checking packages in '%s'...\n",pkgs->identifier);
	}
	data.referencesdb = referencesdb;
	data.filesdb = filesdb;
	data.packagesdb = pkgs;
	data.target = target;
	result = packages_foreach(pkgs,checkpkg,&data,force);

	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	return result;
}
