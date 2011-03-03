/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007 Bernhard R. Link
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
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "binaries.h"
#include "names.h"
#include "dpkgversions.h"
#include "override.h"
#include "tracking.h"
#include "debfile.h"

extern int verbose;

/* get md5sums out of a "Packages.gz"-chunk. */
static retvalue binaries_parse_md5sum(const char *chunk,/*@out@*/struct strlist *md5sums) {
	retvalue r;
	/* collect the given md5sum and size */

	char *pmd5,*psize,*md5sum;

	r = chunk_getvalue(chunk,"MD5sum",&pmd5);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'MD5sum'-line in binary control chunk:\n '%s'\n",chunk);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	r = chunk_getvalue(chunk,"Size",&psize);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Size'-line in binary control chunk:\n '%s'\n",chunk);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		free(pmd5);
		return r;
	}
	md5sum = calc_concatmd5andsize(pmd5,psize);
	free(pmd5);free(psize);
	if( md5sum == NULL ) {
		return RET_ERROR_OOM;
	}
	r = strlist_init_singleton(md5sum,md5sums);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	return RET_OK;
}

/* get somefields out of a "Packages.gz"-chunk. returns RET_OK on success, RET_NOTHING if incomplete, error otherwise */
static retvalue binaries_parse_chunk(const char *chunk,const char *packagename,const char *packagetype,const char *version,/*@out@*/char **sourcename,/*@out@*/char **basename) {
	retvalue r;
	char *parch;
	char *mysourcename,*mybasename;

	assert(packagename!=NULL);

	/* get the sourcename */
	r = chunk_getname(chunk,"Source",&mysourcename,TRUE);
	if( r == RET_NOTHING ) {
		mysourcename = strdup(packagename);
		if( mysourcename == NULL )
			r = RET_ERROR_OOM;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	/* generate a base filename based on package,version and architecture */
	r = chunk_getvalue(chunk,"Architecture",&parch);
	if( !RET_IS_OK(r) ) {
		free(mysourcename);
		return r;
	}
	r = properpackagename(packagename);
	if( !RET_WAS_ERROR(r) )
		r = properversion(version);
	if( !RET_WAS_ERROR(r) )
		r = properfilenamepart(parch);
	if( RET_WAS_ERROR(r) ) {
		free(parch);
		return r;
	}
	mybasename = calc_binary_basename(packagename,version,parch,packagetype);
	free(parch);
	if( mybasename == NULL ) {
		free(mysourcename);
		return RET_ERROR_OOM;
	}

	*basename = mybasename;
	*sourcename = mysourcename;
	return RET_OK;
}

/* get files out of a "Packages.gz"-chunk. */
static retvalue binaries_parse_getfilekeys(const char *chunk,struct strlist *files) {
	retvalue r;
	char *filename;

	/* Read the filename given there */
	r = chunk_getvalue(chunk,"Filename",&filename);
	if( !RET_IS_OK(r) ) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"Does not look like binary control: '%s'\n",chunk);
			r = RET_ERROR;
		}
		return r;
	}
	r = strlist_init_singleton(filename,files);
	return r;
}

static retvalue calcfilekeys(const char *component,const char *sourcename,const char *basename,struct strlist *filekeys) {
	char *filekey;
	retvalue r;

	r = propersourcename(sourcename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	filekey =  calc_filekey(component,sourcename,basename);
	if( filekey == NULL )
		return RET_ERROR_OOM;
	r = strlist_init_singleton(filekey,filekeys);
	return r;
}

static inline retvalue calcnewcontrol(const char *chunk,const char *sourcename,const char *basename,const char *component,struct strlist *filekeys,char **newchunk) {
	retvalue r;

	r = calcfilekeys(component,sourcename,basename,filekeys);
	if( RET_WAS_ERROR(r) )
		return r;

	assert( filekeys->count == 1 );
	*newchunk = chunk_replacefield(chunk,"Filename",filekeys->values[0]);
	if( *newchunk == NULL ) {
		strlist_done(filekeys);
		return RET_ERROR_OOM;
	}
	return RET_OK;
}

retvalue binaries_getname(UNUSED(struct target *t),const char *control,char **packagename){
	retvalue r;

	r = chunk_getvalue(control,"Package",packagename);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find Package name in chunk:'%s'\n",control);
		return RET_ERROR;
	}
	return r;
}
retvalue binaries_getversion(UNUSED(struct target *t),const char *control,char **version) {
	retvalue r;

	r = chunk_getvalue(control,"Version",version);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find Version in chunk:'%s'\n",control);
		return RET_ERROR;
	}
	return r;
}

retvalue binaries_getinstalldata(struct target *t,const char *packagename,const char *version,const char *chunk,char **control,struct strlist *filekeys,struct strlist *md5sums,struct strlist *origfiles) {
	char *sourcename,*basename;
	struct strlist mymd5sums;
	retvalue r;

	r = binaries_parse_md5sum(chunk,&mymd5sums);
	if( RET_WAS_ERROR(r) )
		return r;
	r = binaries_parse_chunk(chunk,packagename,t->packagetype,version,&sourcename,&basename);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&mymd5sums);
		return r;
	} else if( r == RET_NOTHING ) {
		fprintf(stderr,"Does not look like a binary package: '%s'!\n",chunk);
		return RET_ERROR;
	}
	r = binaries_parse_getfilekeys(chunk,origfiles);
	if( RET_WAS_ERROR(r) ) {
		free(sourcename);free(basename);
		strlist_done(&mymd5sums);
		return r;
	}

	r = calcnewcontrol(chunk,sourcename,basename,t->component,filekeys,control);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&mymd5sums);
	} else {
		assert( r != RET_NOTHING );
		strlist_move(md5sums,&mymd5sums);
	}
	free(sourcename);free(basename);
	return r;
}

retvalue binaries_getfilekeys(UNUSED(struct target *t),const char *chunk,struct strlist *filekeys,struct strlist *md5sums) {
	retvalue r;
	r = binaries_parse_getfilekeys(chunk,filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	if( md5sums == NULL )
		return r;
	r = binaries_parse_md5sum(chunk,md5sums);
	return r;
}
char *binaries_getupstreamindex(UNUSED(struct target *target),const char *suite_from,
		const char *component_from,const char *architecture) {
	return mprintf("dists/%s/%s/binary-%s/Packages.gz",suite_from,component_from,architecture);
}
char *ubinaries_getupstreamindex(UNUSED(struct target *target),const char *suite_from,
		const char *component_from,const char *architecture) {
	return mprintf("dists/%s/%s/debian-installer/binary-%s/Packages.gz",suite_from,component_from,architecture);
}

retvalue binaries_doreoverride(const struct distribution *distribution,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk) {
	const struct overrideinfo *o;
	struct fieldtoadd *fields;
	char *newchunk;

	if( interrupted() )
		return RET_ERROR_INTERUPTED;

	o = override_search(distribution->overrides.deb, packagename);
	if( o == NULL )
		return RET_NOTHING;

	fields = override_addreplacefields(o,NULL);
	if( fields == NULL )
		return RET_ERROR_OOM;
	newchunk = chunk_replacefields(controlchunk,fields,"Description");
	addfield_free(fields);
	if( newchunk == NULL )
		return RET_ERROR_OOM;
	*newcontrolchunk = newchunk;
	return RET_OK;
}

retvalue ubinaries_doreoverride(const struct distribution *distribution,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk) {
	const struct overrideinfo *o;
	struct fieldtoadd *fields;
	char *newchunk;

	if( interrupted() )
		return RET_ERROR_INTERUPTED;

	o = override_search(distribution->overrides.udeb, packagename);
	if( o == NULL )
		return RET_NOTHING;

	fields = override_addreplacefields(o,NULL);
	if( fields == NULL )
		return RET_ERROR_OOM;
	newchunk = chunk_replacefields(controlchunk,fields,"Description");
	addfield_free(fields);
	if( newchunk == NULL )
		return RET_ERROR_OOM;
	*newcontrolchunk = newchunk;
	return RET_OK;
}

retvalue binaries_retrack(UNUSED(struct target *t),const char *packagename,const char *chunk, trackingdb tracks,references refs) {
	retvalue r;
	const char *sourcename;
	char *fsourcename,*sourceversion,*arch,*filekey;
	enum filetype filetype;
	struct trackedpackage *pkg;

	//TODO: elliminate duplicate code!
	assert(packagename!=NULL);

	if( interrupted() )
		return RET_ERROR_INTERUPTED;

	/* is there a sourcename */
	r = chunk_getnameandversion(chunk,"Source",&fsourcename,&sourceversion);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		sourceversion = NULL;
		sourcename = packagename;
		fsourcename = NULL;
	} else {
		sourcename = fsourcename;
	}
	if( sourceversion == NULL ) {
		// Think about binNMUs, can something be done here?
		r = chunk_getvalue(chunk,"Version",&sourceversion);
		if( RET_WAS_ERROR(r) ) {
			free(fsourcename);
			return r;
		}
		if( r == RET_NOTHING ) {
			free(fsourcename);
			fprintf(stderr,"Did not find Version in chunk:'%s'\n",chunk);
			return RET_ERROR;
		}
	}

	r = chunk_getvalue(chunk,"Architecture",&arch);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find Architecture in chunk:'%s'\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(sourceversion);
		free(fsourcename);
		return r;
	}
	if( strcmp(arch,"all") == 0 ) {
		filetype = ft_ALL_BINARY;
	} else {
		filetype = ft_ARCH_BINARY;
	}
	free(arch);

	r = chunk_getvalue(chunk,"Filename",&filekey);
	if( !RET_IS_OK(r) ) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"Did not find a Filename in chunk: '%s'\n",chunk);
			r = RET_ERROR;
		}
		free(sourceversion);
		free(fsourcename);
		return r;
	}
	r = tracking_getornew(tracks,sourcename,sourceversion,&pkg);
	free(fsourcename);
	free(sourceversion);
	if( RET_WAS_ERROR(r) ) {
		free(filekey);
		return r;
	}
	assert( r != RET_NOTHING );
	r = trackedpackage_addfilekey(tracks,pkg,filetype,filekey,TRUE,refs);
	if( RET_WAS_ERROR(r) ) {
		trackedpackage_free(pkg);
		return r;
	}
	return tracking_save(tracks, pkg);
}

retvalue binaries_getsourceandversion(UNUSED(struct target *t),const char *chunk,const char *packagename,char **source,char **version) {
	retvalue r;
	char *sourcename,*sourceversion;

	//TODO: elliminate duplicate code!
	assert(packagename!=NULL);

	/* is there a sourcename */
	r = chunk_getnameandversion(chunk,"Source",&sourcename,&sourceversion);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		sourceversion = NULL;
		sourcename = strdup(packagename);
		if( sourcename == NULL )
			return RET_ERROR_OOM;
	}
	if( sourceversion == NULL ) {
		r = chunk_getvalue(chunk,"Version",&sourceversion);
		if( RET_WAS_ERROR(r) ) {
			free(sourcename);
			return r;
		}
		if( r == RET_NOTHING ) {
			free(sourcename);
			fprintf(stderr,"Did not find Version in chunk:'%s'\n",chunk);
			return RET_ERROR;
		}
	}
	*source = sourcename;
	*version = sourceversion;
	return RET_OK;
}

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

void binaries_debdone(struct deb_headers *pkg) {
	free(pkg->name);free(pkg->version);
	free(pkg->source);free(pkg->sourceversion);
	free(pkg->architecture);
	free(pkg->control);
	free(pkg->section);
	free(pkg->priority);
}

retvalue binaries_readdeb(struct deb_headers *deb, const char *filename, bool_t needssourceversion) {
	retvalue r;

	r = extractcontrol(&deb->control,filename);
	if( RET_WAS_ERROR(r) )
		return r;
	/* first look for fields that should be there */

	r = chunk_getname(deb->control,"Package",&deb->name,FALSE);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Package' field in %s!\n",filename);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	r = checkvalue(filename,deb->control,"Maintainer");
	if( RET_WAS_ERROR(r) )
		return r;
	r = checkvalue(filename,deb->control,"Description");
	if( RET_WAS_ERROR(r) )
		return r;
	r = getvalue(filename,deb->control,"Version",&deb->version);
	if( RET_WAS_ERROR(r) )
		return r;
	r = getvalue(filename,deb->control,"Architecture",&deb->architecture);
	if( RET_WAS_ERROR(r) )
		return r;
	/* can be there, otherwise we also know what it is */
	if( needssourceversion )
		r = chunk_getnameandversion(deb->control,"Source",&deb->source,&deb->sourceversion);
	else
		r = chunk_getname(deb->control,"Source",&deb->source,TRUE);
	if( r == RET_NOTHING ) {
		deb->source = strdup(deb->name);
		if( deb->source == NULL )
			r = RET_ERROR_OOM;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	if( needssourceversion && deb->sourceversion == NULL ) {
		deb->sourceversion = strdup(deb->version);
		if( deb->sourceversion == NULL )
			return RET_ERROR_OOM;
	}

	/* normaly there, but optional: */

	r = getvalue_n(deb->control,PRIORITY_FIELDNAME,&deb->priority);
	if( RET_WAS_ERROR(r) )
		return r;
	r = getvalue_n(deb->control,SECTION_FIELDNAME,&deb->section);
	if( RET_WAS_ERROR(r) )
		return r;
	return RET_OK;
}

/* do overwrites, add Filename, Size and md5sum to the control-item */
retvalue binaries_complete(const struct deb_headers *pkg,const char *filekey,const char *md5sum,const struct overrideinfo *override,const char *section,const char *priority, char **newcontrol) {
	const char *size;
	struct fieldtoadd *replace;
	char *newchunk;

	assert( section != NULL && priority != NULL);
	assert( filekey != NULL && md5sum != NULL);

	size = md5sum;
	while( !xisblank(*size) && *size != '\0' )
		size++;
	replace = addfield_newn("MD5sum", md5sum, size-md5sum,NULL);
	if( replace == NULL )
		return RET_ERROR_OOM;
	while( *size != '\0' && xisblank(*size) )
		size++;
	replace = addfield_new("Size", size, replace);
	if( replace == NULL )
		return RET_ERROR_OOM;
	replace = addfield_new("Filename", filekey,replace);
	if( replace == NULL )
		return RET_ERROR_OOM;
	replace = addfield_new(SECTION_FIELDNAME, section ,replace);
	if( replace == NULL )
		return RET_ERROR_OOM;
	replace = addfield_new(PRIORITY_FIELDNAME, priority, replace);
	if( replace == NULL )
		return RET_ERROR_OOM;

	replace = override_addreplacefields(override,replace);
	if( replace == NULL )
		return RET_ERROR_OOM;

	newchunk  = chunk_replacefields(pkg->control,replace,"Description");
	addfield_free(replace);
	if( newchunk == NULL ) {
		return RET_ERROR_OOM;
	}

	*newcontrol = newchunk;

	return RET_OK;
}

retvalue binaries_adddeb(const struct deb_headers *deb,const char *dbdir,references refs,const char *forcearchitecture,const char *packagetype,struct distribution *distribution,struct strlist *dereferencedfilekeys,struct trackingdata *trackingdata,const char *component,const struct strlist *filekeys, const char *control) {
	retvalue r,result;
	int i;

	/* finally put it into one or more architectures of the distribution */

	result = RET_NOTHING;

	if( strcmp(deb->architecture,"all") != 0 ) {
		struct target *t = distribution_getpart(distribution,
				component, deb->architecture,
				packagetype);
		r = target_initpackagesdb(t,dbdir);
		if( !RET_WAS_ERROR(r) ) {
			retvalue r2;
			if( interrupted() )
				r = RET_ERROR_INTERUPTED;
			else
				r = target_addpackage(t, refs, deb->name,
						deb->version,
						control,
						filekeys, FALSE,
						dereferencedfilekeys,
						trackingdata, ft_ARCH_BINARY);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r,r2);
		}
		RET_UPDATE(result,r);
	} else if( forcearchitecture != NULL && strcmp(forcearchitecture,"all") != 0 ) {
		struct target *t = distribution_getpart(distribution,
				component, forcearchitecture,
				packagetype);
		r = target_initpackagesdb(t,dbdir);
		if( !RET_WAS_ERROR(r) ) {
			retvalue r2;
			if( interrupted() )
				r = RET_ERROR_INTERUPTED;
			else
				r = target_addpackage(t, refs, deb->name,
						deb->version,
						control,
						filekeys, FALSE,
						dereferencedfilekeys,
						trackingdata, ft_ALL_BINARY);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r,r2);
		}
		RET_UPDATE(result,r);
	} else for( i = 0 ; i < distribution->architectures.count ; i++ ) {
		/*@dependent@*/struct target *t;
		if( strcmp(distribution->architectures.values[i],"source") == 0 )
			continue;
		t = distribution_getpart(distribution,component,distribution->architectures.values[i],packagetype);
		r = target_initpackagesdb(t,dbdir);
		if( !RET_WAS_ERROR(r) ) {
			retvalue r2;
			if( interrupted() )
				r = RET_ERROR_INTERUPTED;
			else
				r = target_addpackage(t, refs, deb->name,
						deb->version,
						control,
						filekeys, FALSE,
						dereferencedfilekeys,
						trackingdata, ft_ALL_BINARY);
			r2 = target_closepackagesdb(t);
			RET_ENDUPDATE(r,r2);
		}
		RET_UPDATE(result,r);
	}
	RET_UPDATE(distribution->status, result);

	return result;
}

retvalue binaries_calcfilekeys(const char *component,const struct deb_headers *deb,const char *packagetype,struct strlist *filekeys) {
	retvalue r;
	char *basename;

	basename = calc_binary_basename(deb->name, deb->version,
			deb->architecture, packagetype);
	if( basename == NULL )
		return RET_ERROR_OOM;

	r = calcfilekeys(component, deb->source, basename, filekeys);
	free(basename);
	return r;
}

