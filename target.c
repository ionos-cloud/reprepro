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
#include "md5sum.h"
#include "dpkgversions.h"
#include "target.h"

extern int verbose;

static retvalue target_initialize(
	const char *codename,const char *component,const char *architecture,
	get_name getname,get_version getversion,get_installdata getinstalldata,
	get_filekeys getfilekeys, char *directory, const char *indexfile, int unc ,  target *d) {

	target t;

	assert(indexfile);
	if( directory == NULL )
		return RET_ERROR_OOM;

	t = calloc(1,sizeof(struct s_target));
	if( t == NULL ) {
		free(directory);
		return RET_ERROR_OOM;
	}
	t->directory = directory;
	t->indexfile = indexfile;
	if( unc ) {
		t->compressions[ic_uncompressed] = 1;
	}
	t->compressions[ic_gzip] = 1;
	t->codename = strdup(codename);
	t->component = strdup(component);
	t->architecture = strdup(architecture);
	t->identifier = calc_identifier(codename,component,architecture);
	if( !t->codename|| !t->component|| !t->architecture|| !t->identifier) {
		target_free(t);
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
	return target_initialize(codename,component,architecture,binaries_getname,binaries_getversion,binaries_getinstalldata,binaries_getfilekeys,mprintf("%s/%s/binary-%s",codename,component,architecture),"Packages",1,target);
}

retvalue target_initialize_source(const char *codename,const char *component,target *target) {
	return target_initialize(codename,component,"source",sources_getname,sources_getversion,sources_getinstalldata,sources_getfilekeys,mprintf("%s/%s/source",codename,component),"Sources",0,target);
}


void target_free(target target) {
	if( target == NULL )
		return;
	free(target->codename);
	free(target->component);
	free(target->architecture);
	free(target->identifier);
	free(target->directory);
	free(target);
}

/* This opens up the database, if db != NULL, *db will be set to it.. */
retvalue target_initpackagesdb(target target, const char *dbdir, packagesdb *db) {
	retvalue r;

	if( target->packages != NULL ) 
		r = RET_OK;
	else {
		r = packages_initialize(&target->packages,dbdir,target->identifier);
		if( RET_WAS_ERROR(r) ) {
			target->packages = NULL;
			return r;
		}
	}
	if( db )
		*db = target->packages;
	return r;
}

retvalue target_addpackage(target target,DB *references,filesdb files,const char *name,const char *version,const char *control,const struct strlist *filekeys,const struct strlist *md5sums,int force,int downgrade) {
	struct strlist oldfilekeys,*ofk;
	char *oldcontrol;
	retvalue r;

	assert(target->packages);

	if( md5sums != NULL ) {
		r = files_expectfiles(files,filekeys,md5sums);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	r = packages_get(target->packages,name,&oldcontrol);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING )
		ofk = NULL;
	else {
		char *oldversion;

		r = target->getversion(target,oldcontrol,&oldversion);
		if( RET_WAS_ERROR(r) && !force ) {
			free(oldcontrol);
			return r;
		}
		if( RET_IS_OK(r) ) {
			r = dpkgversions_isNewer(version,oldversion);
			if( RET_WAS_ERROR(r) ) {
				fprintf(stderr,"Parse errors processing versions of %s.\n",name);
				if( !force ) {
					free(oldversion);
					free(oldcontrol);
					return r;
				}
			} else {
				if( r == RET_NOTHING ) {
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
	r = packages_insert(references,target->packages,name,control,filekeys,&oldfilekeys);
	if( ofk )
		strlist_done(ofk);
	return r;
}

/* rereference a full database */
struct data_reref { 
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

retvalue target_rereference(target target,DB *referencesdb,int force) {
	retvalue result,r;
	struct data_reref refdata;

	assert(target->packages);

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
	refdata.target = target;
	r = packages_foreach(target->packages,rereferencepkg,&refdata,force);
	RET_UPDATE(result,r);
	
	return result;
}

/* check a full database */
struct data_check { 
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

retvalue target_check(target target,filesdb filesdb,DB *referencesdb,int force) {
	struct data_check data;

	assert(target->packages);
	if( verbose > 1 ) {
		fprintf(stderr,"Checking packages in '%s'...\n",target->identifier);
	}
	data.referencesdb = referencesdb;
	data.filesdb = filesdb;
	data.target = target;
	return packages_foreach(target->packages,checkpkg,&data,force);
}
/* export a database */
retvalue target_export(target target,const char *distdir,int force) {
	indexcompression compression;
	retvalue result,r;

	assert(target && target->packages);
	result = RET_NOTHING;

	for( compression = 0 ; compression <= ic_max ; compression++) {
		if( target->compressions[compression] ) {
			char * filename;

			filename = calc_comprconcat(distdir,target->directory,
					target->indexfile,compression);
			if( filename == NULL )
				return RET_ERROR_OOM;
			r = packages_export(target->packages,filename,compression);
			free(filename);
			RET_UPDATE(result,r);
			if( !force && RET_WAS_ERROR(r) )
				return r;
		}
	}
	return result;
}

static inline retvalue printfilemd5(target target,const char *distdir,FILE *out,
		const char *filename,indexcompression compression) {
	char *fn,*md;
	const char *fn2;
	retvalue r;

	fn = calc_comprconcat(distdir,target->directory,filename,compression);
	if( fn == NULL )
		return RET_ERROR_OOM;
	// well, a crude hack, but not too bad:
	fn2 = fn + strlen(distdir) + strlen(target->codename) + 2; 

	r = md5sum_read(fn,&md);

	if( RET_IS_OK(r) ) {
		fprintf(out," %s %s\n",md,fn2);
		free(md);
	} else {
		fprintf(stderr,"Error processing %s\n",fn);
		if( r == RET_NOTHING ) 
			r = RET_ERROR_MISSING;
	}
	free(fn);
	return r;
}

retvalue target_printmd5sums(target target,const char *distdir,FILE *out,int force) {
	indexcompression compression;
	retvalue result,r;

	result = printfilemd5(target,distdir,out,"Release",ic_uncompressed);

	if( RET_WAS_ERROR(result) && !force)
		return result;

	for( compression = 0 ; compression <= ic_max ; compression++) {
		if( target->compressions[compression] ) {
			r = printfilemd5(target,distdir,out,target->indexfile,compression);

			RET_UPDATE(result,r);
			if( !force && RET_WAS_ERROR(r) )
				return r;
		}
	}
	return result;
}
