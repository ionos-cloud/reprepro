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
// create  the chunk of the Packages.gz-file and 
// putting it in the various databases.


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

static inline retvalue getvalue_n(const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		*value = NULL;
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

	r = checkvalue(filename,deb->control,"Priority");
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	r = getvalue_n(deb->control,"Section",&deb->section);
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


/* Guess which component to use:
 * - if the user gave one, use that one.
 * - if the section is a componentname, use this one
 * - if the section starts with a componentname/, use this one
 * - if the section ends with a /componentname, use this one
 * - if the section/ is the start of a componentname, use this one
 * - use the first component in the list
 */

retvalue guess_component(struct distribution *distribution, 
			 struct debpackage *package,
			 const char *givencomponent) {
	int i;
	const char *section;
	size_t section_len;

#define RETURNTHIS(comp) { \
		char *c = strdup(comp); \
		if( !c ) \
			return RET_ERROR_OOM; \
		free(package->component); \
		package->component = c; \
		return RET_OK; \
	}
	
	if( givencomponent ) {
		if( givencomponent && !strlist_in(&distribution->components,givencomponent) ) {
			fprintf(stderr,"Could not find '%s' in components of '%s': ",
					givencomponent,distribution->codename);
			strlist_fprint(stderr,&distribution->components);
			fputs("'\n",stderr);
			return RET_ERROR;
		}

		RETURNTHIS(givencomponent);
	}
	section = package->section;
	if( section == NULL ) {
		fprintf(stderr,"Found no section for '%s', so I cannot guess the component to put it in!\n",package->package);
		return RET_ERROR;
	}
	if( distribution->components.count <= 0 ) {
		fprintf(stderr,"I do not find any components in '%s', so there is no chance I cannot even take one by guessing!\n",distribution->codename);
		return RET_ERROR;
	}
	section_len = strlen(section);

	for( i = 0 ; i < distribution->components.count ; i++ ) {
		const char *component = distribution->components.values[i];

		if( strcmp(section,component) == 0 )
			RETURNTHIS(component);
	}
	for( i = 0 ; i < distribution->components.count ; i++ ) {
		const char *component = distribution->components.values[i];
		size_t len = strlen(component);

		if( len<section_len && section[len] == '/' && strncmp(section,component,len) == 0 )
			RETURNTHIS(component);
	}
	for( i = 0 ; i < distribution->components.count ; i++ ) {
		const char *component = distribution->components.values[i];
		size_t len = strlen(component);

		if( len<section_len && section[section_len-len-1] == '/' && 
				strncmp(section+section_len-len,component,len) == 0 )
			RETURNTHIS(component);
	}
	for( i = 0 ; i < distribution->components.count ; i++ ) {
		const char *component = distribution->components.values[i];

		if( strncmp(section,component,section_len) == 0 && component[section_len] == '/' )
			RETURNTHIS(component);
	}
	RETURNTHIS(distribution->components.values[0]);
#undef RETURNTHIS
}

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <d->architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if component is NULL, guessing it from the section. */

retvalue deb_add(const char *dbdir,DB *references,DB *filesdb,const char *mirrordir,const char *forcecomponent,struct distribution *distribution,const char *debfilename,int force){
	retvalue r,result;
	struct debpackage *pkg;
	char *md5andsize;
	struct strlist filekeys;
	int i;

	/* First taking a closer look to the file: */

	r = deb_read(&pkg,debfilename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	/* look for overwrites */

	// TODO: look for overwrites and things like this here...
	// TODO: set pkg->section to new value if doing so.
	
	/* decide where it has to go */

	r = guess_component(distribution,pkg,forcecomponent);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	}
	if( verbose > 0 && forcecomponent == NULL ) {
		fprintf(stderr,"%s: component guessed as '%s'\n",debfilename,pkg->component);
	}
	
	/* some sanity checks: */

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
	binaries_calcfilekeys(pkg->component,pkg->source,pkg->basename,&filekeys);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	}

	/* then looking if we already have this, or copy it in */

	r = files_checkin(filesdb,mirrordir,filekeys.values[0],debfilename,&md5andsize);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&filekeys);
		deb_free(pkg);
		return r;
	} 

	r = deb_complete(pkg,filekeys.values[0],md5andsize);
	free(md5andsize);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&filekeys);
		deb_free(pkg);
		return r;
	} 
	
	/* finaly put it into one or more distributions */

	result = RET_NOTHING;

	if( strcmp(pkg->architecture,"all") != 0 ) {
		r = binaries_addtodist(dbdir,references,distribution->codename,pkg->component,pkg->architecture,pkg->package,pkg->version,pkg->control,&filekeys);
		RET_UPDATE(result,r);
	} else for( i = 0 ; i < distribution->architectures.count ; i++ ) {
		r = binaries_addtodist(dbdir,references,distribution->codename,pkg->component,distribution->architectures.values[i],pkg->package,pkg->version,pkg->control,&filekeys);
		RET_UPDATE(result,r);
	}

	strlist_done(&filekeys);
	deb_free(pkg);

	return result;
}
