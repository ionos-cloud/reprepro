/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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
#include "error.h"
#include "strlist.h"
#include "md5sum.h"
#include "names.h"
#include "chunks.h"
#include "checkindeb.h"
#include "reference.h"
#include "binaries.h"
#include "files.h"
#include "extractcontrol.h"
#include "guesscomponent.h"

extern int verbose;

/* This file includes the code to include binaries, i.e.
   to create the chunk for the Packages.gz-file and 
   to put it in the various databases.
   
Things to do with .deb's checkin by hand: (by comparison with apt-ftparchive)
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

static inline retvalue getvalue_n(const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		*value = NULL;
	}
	return r;
}

struct debpackage {
	/* things to be set by deb_read: */
	char *package,*version,*source,*architecture;
	char *control;
	/* things that might still be NULL then: */
	char *section;
	char *priority;
	/* things that will still be NULL then: */
	char *component; //This might be const, too and save some strdups, but...
	/* with deb_calclocations: */
	const char *filekey;
	struct strlist filekeys;
	/* with deb_copyfiles or deb_checkfiles: */
	char *md5sum;
};

void deb_free(struct debpackage *pkg) {
	if( pkg ) {
		free(pkg->package);free(pkg->version);
		free(pkg->source);free(pkg->architecture);
		free(pkg->control);
		free(pkg->section);free(pkg->component);
		free(pkg->priority);
		if( pkg->filekey )
			strlist_done(&pkg->filekeys);
		free(pkg->md5sum);
	}
	free(pkg);
}

/* read the data from a .deb, make some checks and extract some data */
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

	r = chunk_getname(deb->control,"Package",&deb->package,FALSE);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Package' field in %s!\n",filename);
		r = RET_ERROR;
	}
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
	r = chunk_getname(deb->control,"Source",&deb->source,TRUE);
	if( r == RET_NOTHING ) {
		deb->source = strdup(deb->package);
		if( deb->source == NULL )
			r = RET_ERROR_OOM;
	}
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}

	r = getvalue_n(deb->control,PRIORITY_FIELDNAME,&deb->priority);
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	r = getvalue_n(deb->control,SECTION_FIELDNAME,&deb->section);
	if( RET_WAS_ERROR(r) ) {
		deb_free(deb);
		return r;
	}
	*pkg = deb;

	return RET_OK;
}

/* do overwrites, add Filename, Size and md5sum to the control-item */
retvalue deb_complete(struct debpackage *pkg,const struct overrideinfo *override) {
	const char *size;
	struct fieldtoadd *replace;
	char *newchunk;

	assert( pkg->section != NULL && pkg->priority != NULL);

	size = pkg->md5sum;
	while( !isblank(*size) && *size )
		size++;
	replace = addfield_newn("MD5sum",pkg->md5sum, size-pkg->md5sum,NULL);
	if( !replace )
		return RET_ERROR_OOM;
	while( *size && isblank(*size) )
		size++;
	replace = addfield_new("Size",size,replace);
	if( !replace )
		return RET_ERROR_OOM;
	replace = addfield_new("Filename",pkg->filekey,replace);
	if( !replace )
		return RET_ERROR_OOM;
	replace = addfield_new(SECTION_FIELDNAME,pkg->section ,replace);
	if( !replace )
		return RET_ERROR_OOM;
	replace = addfield_new(PRIORITY_FIELDNAME,pkg->priority, replace);
	if( !replace )
		return RET_ERROR_OOM;

	replace = override_addreplacefields(override,replace);
	if( !replace )
		return RET_ERROR_OOM;

	newchunk  = chunk_replacefields(pkg->control,replace,"Description");
	addfield_free(replace);
	if( newchunk == NULL ) {
		return RET_ERROR_OOM;
	}

	free(pkg->control);
	pkg->control = newchunk;

	return RET_OK;
}

static retvalue deb_calclocations(struct debpackage *pkg,const char *givenfilekey,const char *suffix) {
	retvalue r;
	char *basename;
	
	basename = calc_binary_basename(pkg->package,pkg->version,pkg->architecture,suffix);
	if( basename == NULL )
		return RET_ERROR_OOM;

	r = binaries_calcfilekeys(pkg->component,pkg->source,basename,&pkg->filekeys);
	if( RET_WAS_ERROR(r) )
		return r;

	pkg->filekey = pkg->filekeys.values[0];
	free(basename);

	if( givenfilekey && strcmp(givenfilekey,pkg->filekey) != 0 ) {
		fprintf(stderr,"Name mismatch, .changes indicates '%s', but the file itself says '%s'!\n",givenfilekey,pkg->filekey);
		return RET_ERROR;
	}

	return r;
}

static retvalue deb_copyfiles(filesdb filesdb,struct debpackage *pkg,const char *debfilename, int delete) {
	retvalue r;

	r = files_include(filesdb,debfilename,pkg->filekey,NULL,&pkg->md5sum,delete);
	return r;
}

static retvalue deb_checkfiles(filesdb filesdb,struct debpackage *pkg,const char *md5sum) {
	/* Not much to do here, as anything should already be done... */

	pkg->md5sum = strdup(md5sum);
	if( pkg->md5sum == NULL )
		return RET_ERROR_OOM;
	return RET_OK;
}

/* insert the given .deb into the mirror in <component> in the <distribution>
 * putting things with architecture of "all" into <d->architectures> (and also
 * causing error, if it is not one of them otherwise)
 * if component is NULL, guessing it from the section. */

retvalue deb_add(const char *dbdir,references refs,filesdb filesdb,const char *forcecomponent,const char *forcearchitecture,const char *forcesection,const char *forcepriority,const char *suffix,struct distribution *distribution,const char *debfilename,const char *givenfilekey,const char *givenmd5sum,const struct overrideinfo *binoverride,int force,int delete){
	retvalue r,result;
	struct debpackage *pkg;
	const struct overrideinfo *oinfo;
	const struct strlist *components;
	int i;

	assert( (givenmd5sum && givenfilekey) ||
		(givenmd5sum==NULL && givenfilekey==NULL ) );

	/* First taking a closer look to the file: */

	r = deb_read(&pkg,debfilename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	oinfo = override_search(binoverride,pkg->package);
	if( forcesection == NULL ) {
		forcesection = override_get(oinfo,SECTION_FIELDNAME);
	}
	if( forcepriority == NULL ) {
		forcepriority = override_get(oinfo,PRIORITY_FIELDNAME);
	}

	if( forcesection ) {
		free(pkg->section);
		pkg->section = strdup(forcesection);
		if( pkg->section == NULL ) {
			deb_free(pkg);
			return RET_ERROR_OOM;
		}
	}
	if( forcepriority ) {
		free(pkg->priority);
		pkg->priority = strdup(forcepriority);
		if( pkg->priority == NULL ) {
			deb_free(pkg);
			return RET_ERROR_OOM;
		}
	}

	if( pkg->section == NULL ) {
		fprintf(stderr,"No section was given for '%s', skipping.\n",pkg->package);
		deb_free(pkg);
		return RET_ERROR;
	}
	if( pkg->priority == NULL ) {
		fprintf(stderr,"No priority was given for '%s', skipping.\n",pkg->package);
		deb_free(pkg);
		return RET_ERROR;
	}
	
	/* decide where it has to go */

	if( strcmp(suffix,"udeb") == 0 )
		components = &distribution->udebcomponents;
	else
		components = &distribution->components;

	r = guess_component(distribution->codename,components,
			pkg->package,pkg->section,forcecomponent,&pkg->component);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	}
	if( verbose > 0 && forcecomponent == NULL ) {
		fprintf(stderr,"%s: component guessed as '%s'\n",debfilename,pkg->component);
	}
	
	/* some sanity checks: */

	if( forcearchitecture && strcmp(forcearchitecture,"all") == 0 )
		forcearchitecture = NULL;

	if( forcearchitecture && 
			strcmp(pkg->architecture,forcearchitecture) != 0 &&
			strcmp(pkg->architecture,"all") != 0 ) {
		fprintf(stderr,"Cannot checking in '%s' into architecture '%s', as it is '%s'!",
				debfilename,forcearchitecture,pkg->architecture);
		deb_free(pkg);
		if( delete >= D_DELETE ) {
			if( verbose >= 0 )
				fprintf(stderr,"Deleting '%s' as requested!\n",debfilename);
			if( unlink(debfilename) != 0 ) {
				fprintf(stderr,"Error deleting '%s': %m\n",debfilename);
			}
		}
		return RET_ERROR;
	} else if( strcmp(pkg->architecture,"all") != 0 &&
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
	if( !strlist_in(components,pkg->component) ) {
		fprintf(stderr,"While checking in '%s': Would put in component '%s', but that is not available!\n",debfilename,pkg->component);
		/* this cannot be ignored on force as there is not data structure available*/
		return RET_ERROR;
	}

	r = deb_calclocations(pkg,givenfilekey,suffix);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	}

	/* then looking if we already have this, or copy it in */

	if( givenfilekey && givenmd5sum ) {
		assert( delete == D_INPLACE );
		r = deb_checkfiles(filesdb,pkg,givenmd5sum);
	} else
		r = deb_copyfiles(filesdb,pkg,debfilename,delete);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	} 

	r = deb_complete(pkg,oinfo);
	if( RET_WAS_ERROR(r) ) {
		deb_free(pkg);
		return r;
	} 
	
	/* finaly put it into one or more architectures of the distribution */

	result = RET_NOTHING;

	if( strcmp(pkg->architecture,"all") != 0 ) {
		struct target *t = distribution_getpart(distribution,pkg->component,pkg->architecture,suffix);
		r = target_initpackagesdb(t,dbdir);
		if( !RET_WAS_ERROR(r) ) {
			retvalue r2;
			r = target_addpackage(t,refs,pkg->package,pkg->version,pkg->control,&pkg->filekeys,force,FALSE);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r,r2);
		}
		RET_UPDATE(result,r);
	} else if( forcearchitecture ) {
		struct target *t = distribution_getpart(distribution,pkg->component,forcearchitecture,suffix);
		r = target_initpackagesdb(t,dbdir);
		if( !RET_WAS_ERROR(r) ) {
			retvalue r2;
			r = target_addpackage(t,refs,pkg->package,pkg->version,pkg->control,&pkg->filekeys,force,FALSE);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r,r2);
		}
		RET_UPDATE(result,r);
	} else for( i = 0 ; i < distribution->architectures.count ; i++ ) {
		struct target *t;
		if( strcmp(distribution->architectures.values[i],"source") == 0 )
			continue;
		t = distribution_getpart(distribution,pkg->component,distribution->architectures.values[i],suffix);
		r = target_initpackagesdb(t,dbdir);
		if( !RET_WAS_ERROR(r) ) {
			retvalue r2;
			r = target_addpackage(t,refs,pkg->package,pkg->version,pkg->control,&pkg->filekeys,force,FALSE);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r,r2);
		}
		RET_UPDATE(result,r);
	}

	deb_free(pkg);

	return result;
}
