/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2003 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>
#include <zlib.h>
#include <db.h>
#include "error.h"
#include "md5sum.h"
#include "names.h"
#include "chunks.h"
#include "checkindeb.h"
#include "reference.h"
#include "packages.h"
#include "binaries.h"
#include "files.h"
#include "extractcontrol.h"

extern int verbose;

// This file shall include the code to include binaries, i.e.
// create or adopt the chunk of the Packages.gz-file and 
// putting it in the various databases.

/* Add <package> with filename <filekey> and chunk <chunk> (which
 * alreadycontains "Filename:") and characteristica <md5andsize>
 * to the <files> database, add an reference to <referee> in 
 * <references> and overwrite/add it to <pkgs> removing
 * references to oldfilekey that will be fall out of it by this */

retvalue checkindeb_insert(DB *references,const char *referee,
		           DB *pkgs,
		const char *packagename, const char *chunk,
		const char *filekey, const char *oldfilekey) {

	retvalue result,r;


	/* mark it as needed by this distribution */

	if( oldfilekey == NULL || strcmp(filekey,oldfilekey) != 0 ) {
		r = references_increment(references,filekey,referee);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	/* Add package to distribution's database */

	// Todo: do this earlier...
	if( oldfilekey != NULL ) {
		result = packages_replace(pkgs,packagename,chunk);

	} else {
		result = packages_add(pkgs,packagename,chunk);
	}

	if( RET_WAS_ERROR(result) )
		return result;
		
	/* remove old references to files */
	if( oldfilekey != NULL && strcmp(filekey,oldfilekey) != 0) {
		r = references_decrement(references,oldfilekey,referee);
		RET_UPDATE(result,r);
	}

	return result;
}


retvalue checkindeb_addChunk(DB *packagesdb, DB *referencesdb,DB *filesdb, const char *identifier,const char *mirrordir,const char *chunk,const char *packagename, const char *filekey,const struct strlist *filekeys, const struct strlist *md5sums,const struct strlist *oldfilekeys){
	char *newchunk;
	retvalue result,r;

	/* look for needed files */

	r = files_insert(filesdb,mirrordir,filekeys,md5sums);
	if( RET_WAS_ERROR(r) )
		return r;
	
	/* write in its position and check it in */

	newchunk = chunk_replacefield(chunk,"Filename",filekey);
	if( !newchunk )
		return RET_ERROR;

	result = packages_insert(identifier,referencesdb,packagesdb,
			packagename,newchunk,filekeys,oldfilekeys);

	free(newchunk);
	return result;
}

static retvalue deb_addtodist(const char *dbpath,DB *references,struct distribution *distribution,const char *component,const char *architecture,struct debpackage *package,const struct strlist *filekeys) {
	retvalue result,r;
	char *identifier,*oldversion;
	DB *packages;
	struct strlist oldfilekeys,*o;

	identifier = calc_identifier(distribution->codename,component,architecture);
	if( ! identifier )
		return RET_ERROR_OOM;

	packages = packages_initialize(dbpath,identifier);
	if( ! packages ) {
		free(identifier);
		return RET_ERROR;
	}

	r = binaries_lookforolder(packages,package->package,package->version,&oldversion,&oldfilekeys);
	if( RET_WAS_ERROR(r) ) {
		(void)packages_done(packages);
		free(identifier);
		return r;
	}
	if( RET_IS_OK(r) )
		o = &oldfilekeys;
	else
		o = NULL;

	if( RET_IS_OK(r) && oldversion ) {
		fprintf(stderr,"Version '%s' already in the archive, skipping '%s'\n",oldversion,package->version);
		free(oldversion);
		result = RET_NOTHING;
	} else
		result = packages_insert(identifier,references,packages,
			package->package, package->control,
			filekeys, o);

	r = packages_done(packages);
	RET_ENDUPDATE(result,r);

	free(identifier);
	if( o )
		strlist_done(&oldfilekeys);
	return result;
}

/* things to do with .deb's checkin by hand: (by comparison with apt-ftparchive)
- extract the control file (that's the hard part -> extractcontrol.c )
- check for Package, Version, Architecture, Maintainer, Description
- apply overwrite if neccesary (section,priority and perhaps maintainer).
- add Size, MD5sum, Filename, Priority, Section
- remove Status (warning if existant?)
- check for Optional-field and reject then..
*/

static inline retvalue getvalue(const char *filename,const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Cannot find %s-header in control file of %s!\n",field,filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue checkvalue(const char *filename,const char *chunk,const char *field) {
	retvalue r;

	r = chunk_checkfield(chunk,field);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Cannot find %s-header in control file of %s!\n",field,filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue getvalue_d(const char *defaul,const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		*value = strdup(defaul);
		if( *value == NULL )
			r = RET_ERROR_OOM;
	}
	return r;
}

void deb_free(struct debpackage *pkg) {
	if( pkg ) {
		free(pkg->package);free(pkg->version);
		free(pkg->source);free(pkg->architecture);
		free(pkg->basename);free(pkg->control);
	}
	free(pkg);
}

retvalue deb_read(struct debpackage **pkg, const char *filename) {
	retvalue r;
	struct debpackage *deb;


	deb = calloc(1,sizeof(struct debpackage));

	r = extractcontrol(&deb->control,filename);
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}

	/* first look for fields that should be there */

	r = getvalue(filename,deb->control,"Package",&deb->package);
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	r = checkvalue(filename,deb->control,"Maintainer");
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	r = checkvalue(filename,deb->control,"Description");
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	r = getvalue(filename,deb->control,"Version",&deb->version);
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	r = getvalue(filename,deb->control,"Architecture",&deb->architecture);
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}

	/* can be there, otherwise we also know what it is */
	r = getvalue_d(deb->package,deb->control,"Source",&deb->source);
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}

	deb->basename = calc_package_basename(deb->package,deb->version,deb->architecture);
	if( deb->basename == NULL ) {
		deb_free(deb);
		return r;
	}

	/* check for priority and section, compare with defaults
	 * and overrides */

	//TODO ... do so ...

	r = checkvalue(filename,deb->control,"Priority");
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	r = checkvalue(filename,deb->control,"Section");
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	*pkg = deb;

	return RET_OK;
}

retvalue deb_complete(struct debpackage *pkg, const char *filekey, const char *md5andsize) {
	const char *size;
	struct fieldtoadd *file;
	char *newchunk;

	size = md5andsize;
	while( !isblank(*size) && *size )
		size++;
	file = addfield_newn("MD5Sum",md5andsize, size-md5andsize,NULL);
	if( !file )
		return RET_ERROR_OOM;
	while( *size && isblank(*size) )
		size++;
	file = addfield_new("Size",size,file);
	if( !file )
		return RET_ERROR_OOM;
	file = addfield_new("Filename",filekey,file);
	if( !file )
		return RET_ERROR_OOM;

	// TODO: add overwriting of other fields here, (before the rest)
	
	newchunk  = chunk_replacefields(pkg->control,file,"Description");
	addfield_free(file);
	if( newchunk == NULL ) {
		return RET_ERROR_OOM;
	}

	free(pkg->control);
	pkg->control = newchunk;

	return RET_OK;
}

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <d->architectures> (and also
 * causing error, if it is not one of them otherwise)
 * ([todo:]if component is NULL, using translation table <guesstable>)
 * ([todo:]using overwrite-database <overwrite>)*/

retvalue deb_add(const char *dbdir,DB *references,DB *filesdb,const char *mirrordir,const char *component,struct distribution *distribution,const char *debfilename,int force){
	retvalue r,result;
	struct debpackage *pkg;
	char *filekey,*md5andsize;
	struct strlist filekeys;
	int i;

	/* First taking a closer look to the file: */

	r = deb_read(&pkg,debfilename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	/* look for overwrites */

	// TODO: look for overwrites and things like this here...
	
	/* decide where it has to go */

	// TODO: decide what component is, if not yet set...

	/* some sanity checks: */

	if( component && !strlist_in(&distribution->components,component) ) {
		fprintf(stderr,"While checking in '%s': '%s' is not listed in '",
				debfilename,component);
		strlist_fprint(stderr,&distribution->components);
		fputs("'\n",stderr);
		if( force <= 0 ) {
			deb_free(pkg);
			return RET_ERROR;
		}
	}

	if( strcmp(pkg->architecture,"all") != 0 &&
	    !strlist_in( &distribution->architectures, pkg->architecture )) {
		fprintf(stderr,"While checking in '%s': '%s' is not listed in '",
				debfilename,pkg->architecture);
		strlist_fprint(stderr,&distribution->architectures);
		fputs("'\n",stderr);
		if( force <= 0 ) {
			deb_free(pkg);
			return RET_ERROR;
		}
	} 
	
	/* calculate it's filekey */
	filekey = calc_filekey(component,pkg->source,pkg->basename);
	if( filekey == NULL) {
		deb_free(pkg);
		return RET_ERROR_OOM;
	}
	r = strlist_init_singleton(filekey,&filekeys);
	if( RET_WAS_ERROR(r) ) {
		free(filekey);
		deb_free(pkg);
		return r;
	}

	/* then looking if we already have this, or copy it in */

	r = files_checkin(filesdb,mirrordir,filekey,debfilename,&md5andsize);
	if( RET_WAS_ERROR(r) ) {
		free(filekey);
		deb_free(pkg);
		return r;
	} 

	r = deb_complete(pkg,filekey,md5andsize);
	if( RET_WAS_ERROR(r) ) {
		free(filekey);
		free(md5andsize);
		deb_free(pkg);
		return r;
	} 
	free(md5andsize);
	
	/* finaly put it into one or more distributions */

	result = RET_NOTHING;

	if( strcmp(pkg->architecture,"all") != 0 ) {
		r = deb_addtodist(dbdir,references,distribution,component,pkg->architecture,pkg,&filekeys);
		RET_UPDATE(result,r);
	} else for( i = 0 ; i < distribution->architectures.count ; i++ ) {
		r = deb_addtodist(dbdir,references,distribution,component,distribution->architectures.values[i],pkg,&filekeys);
		RET_UPDATE(result,r);
	}

	strlist_done(&filekeys);
	deb_free(pkg);

	return result;
}
