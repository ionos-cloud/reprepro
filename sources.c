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

#include <assert.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "chunks.h"
#include "sources.h"
#include "names.h"
#include "dpkgversions.h"
#include "override.h"
#include "tracking.h"
#include "signature.h"

extern int verbose;


static retvalue getBasenames(const struct strlist *filelines,/*@out@*/struct strlist *basenames) {
	int i;
	retvalue r;

	assert( filelines != NULL && basenames != NULL );

	r = strlist_init_n(filelines->count,basenames);
	if( RET_WAS_ERROR(r) )
		return r;
	r = RET_NOTHING;
	for( i = 0 ; i < filelines->count ; i++ ) {
		char *basename;
		const char *fileline=filelines->values[i];

		r = calc_parsefileline(fileline,&basename,NULL);
		if( RET_WAS_ERROR(r) )
			break;

		r = strlist_add(basenames,basename);
		if( RET_WAS_ERROR(r) ) {
			break;
		}
		r = RET_OK;
	}
	if( RET_WAS_ERROR(r) ) {
		strlist_done(basenames);
	} else {
		assert( filelines->count == basenames->count );
	}
	return r;
}

retvalue sources_calcfilelines(const struct checksumsarray *files, char **item) {
	size_t len;
	int i;
	char *result;

	assert( files != NULL );

	len = 1;
	for( i=0 ; i < files->names.count ; i++ ) {
		const char *md5, *size;
		size_t md5len = 0, sizelen = 0;
		bool found;

		found = checksums_gethashpart(files->checksums[i], cs_md5sum,
				&md5, &md5len, &size, &sizelen);
		assert( found );

		len += 4+strlen(files->names.values[i])+md5len+sizelen;
	}
	result = malloc(len*sizeof(char));
	if( result == NULL )
		return RET_ERROR_OOM;
	*item = result;
	*(result++) = '\n';
	for( i=0 ; i < files->names.count ; i++ ) {
		const char *md5, *size;
		size_t md5len, sizelen;
		bool found;

		*(result++) = ' ';
		found = checksums_gethashpart(files->checksums[i], cs_md5sum,
				&md5, &md5len, &size, &sizelen);
		assert( found );
		memcpy(result, md5, md5len); result += md5len;
		*(result++) = ' ';
		memcpy(result, size, sizelen); result += sizelen;
		*(result++) = ' ';
		strcpy(result, files->names.values[i]);
		result += strlen(files->names.values[i]);
		*(result++) = '\n';
	}
	*(--result) = '\0';
	assert( (size_t)(result - *item) == len-1 );
	return RET_OK;
}

retvalue sources_getname(UNUSED(struct target *t),const char *control,char **packagename){
	retvalue r;

	r = chunk_getvalue(control,"Package",packagename);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Missing 'Package' field in chunk:'%s'\n", control);
		return RET_ERROR;
	}
	return r;
}
retvalue sources_getversion(UNUSED(struct target *t),const char *control,char **version) {
	retvalue r;

	r = chunk_getvalue(control,"Version",version);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Missing 'Version' field in chunk:'%s'\n", control);
		return RET_ERROR;
	}
	return r;
}

retvalue sources_getinstalldata(struct target *t, const char *packagename, UNUSED(const char *version), const char *chunk, char **control, struct strlist *filekeys, struct checksumsarray *origfiles) {
	retvalue r;
	char *origdirectory, *directory, *mychunk;
	struct strlist filelines, myfilekeys;
	struct checksumsarray files;

	r = chunk_getextralinelist(chunk,"Files",&filelines);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Files' entry in '%s'!\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
  		return r;

	r = checksumsarray_parse(&files, &filelines, packagename);
	strlist_done(&filelines);
	if( RET_WAS_ERROR(r) )
		return r;

	r = chunk_getvalue(chunk, "Directory", &origdirectory);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Directory' entry in '%s'!\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		checksumsarray_done(&files);
		return r;
	}

	r = propersourcename(packagename);
	assert( r != RET_NOTHING );
	if( RET_IS_OK(r) )
		r = properfilenames(&files.names);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Forbidden characters in source package '%s'!\n", packagename);
		free(origdirectory);
		checksumsarray_done(&files);
		return r;
	}

	directory = calc_sourcedir(t->component, packagename);
	if( directory == NULL )
		r = RET_ERROR_OOM;
	else
		r = calc_dirconcats(directory, &files.names, &myfilekeys);
	if( RET_WAS_ERROR(r) ) {
		free(directory);
		free(origdirectory);
		checksumsarray_done(&files);
		return r;
	}
	r = calc_inplacedirconcats(origdirectory, &files.names);
	free(origdirectory);
	if( !RET_WAS_ERROR(r) ) {
		mychunk = chunk_replacefield(chunk, "Directory", directory);
		if( mychunk == NULL )
			r = RET_ERROR_OOM;
	}
	free(directory);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&myfilekeys);
		checksumsarray_done(&files);
		return r;
	}
	*control = mychunk;
	strlist_move(filekeys, &myfilekeys);
	checksumsarray_move(origfiles, &files);
	return RET_OK;
}

retvalue sources_getfilekeys(const char *chunk, struct strlist *filekeys) {
	char *origdirectory;
	struct strlist basenames;
	retvalue r;
	struct strlist filelines;


	/* Read the directory given there */
	r = chunk_getvalue(chunk, "Directory", &origdirectory);
	if( r == RET_NOTHING ) {
		//TODO: check if it is even text and do not print
		//of looking binary??
		fprintf(stderr, "Does not look like source control: '%s'\n", chunk);
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;

	r = chunk_getextralinelist(chunk, "Files", &filelines);
	if( r == RET_NOTHING ) {
		//TODO: check if it is even text and do not print
		//of looking binary??
		fprintf(stderr, "Does not look like source control: '%s'\n", chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(origdirectory);
		return r;
	}
	r = getBasenames(&filelines, &basenames);
	strlist_done(&filelines);
	if( RET_WAS_ERROR(r) ) {
		free(origdirectory);
		return r;
	}

	r = calc_dirconcats(origdirectory, &basenames, filekeys);
	free(origdirectory);
	strlist_done(&basenames);
	return r;
}

retvalue sources_getchecksums(const char *chunk, struct checksumsarray *out) {
	char *origdirectory;
	struct checksumsarray a;
	struct strlist filelines;
	retvalue r;

	/* Read the directory given there */
	r = chunk_getvalue(chunk, "Directory", &origdirectory);
	if( !RET_IS_OK(r) )
		return r;

	r = chunk_getextralinelist(chunk,"Files",&filelines);
	if( !RET_IS_OK(r) ) {
		free(origdirectory);
		return r;
	}
	r = checksumsarray_parse(&a, &filelines, "source chunk");
	strlist_done(&filelines);
	if( RET_WAS_ERROR(r) ) {
		free(origdirectory);
		return r;
	}
	r = calc_inplacedirconcats(origdirectory, &a.names);
	free(origdirectory);
	if( RET_WAS_ERROR(r) ) {
		checksumsarray_done(&a);
		return r;
	}
	checksumsarray_move(out, &a);
	return RET_OK;
}

char *sources_getupstreamindex(UNUSED(struct target *target),const char *suite_from,
		const char *component_from,UNUSED(const char *architecture)) {
	return mprintf("dists/%s/%s/source/Sources.gz",suite_from,component_from);
}

retvalue sources_doreoverride(const struct distribution *distribution,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk) {
	const struct overrideinfo *o;
	struct fieldtoadd *fields;
	char *newchunk;

	if( interrupted() )
		return RET_ERROR_INTERRUPTED;

	o = override_search(distribution->overrides.dsc, packagename);
	if( o == NULL )
		return RET_NOTHING;

	fields = override_addreplacefields(o,NULL);
	if( fields == NULL )
		return RET_ERROR_OOM;
	newchunk = chunk_replacefields(controlchunk,fields,"Files");
	addfield_free(fields);
	if( newchunk == NULL )
		return RET_ERROR_OOM;
	*newcontrolchunk = newchunk;
	return RET_OK;
}

retvalue sources_retrack(const char *sourcename, const char *chunk, trackingdb tracks, struct database *database) {
	retvalue r;
	char *sourceversion;
	struct trackedpackage *pkg;
	struct strlist filekeys;
	int i;

	//TODO: elliminate duplicate code!
	assert(sourcename!=NULL);

	if( interrupted() )
		return RET_ERROR_INTERRUPTED;

	r = chunk_getvalue(chunk,"Version",&sourceversion);
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Missing 'Version' field in chunk:'%s'\n", chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	r = sources_getfilekeys(chunk, &filekeys);
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Malformed source control:'%s'\n", chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(sourceversion);
		return r;
	}

	r = tracking_getornew(tracks,sourcename,sourceversion,&pkg);
	free(sourceversion);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&filekeys);
		return r;
	}

	// TODO: error handling is suboptimal here.
	//  is there a way to again remove old additions (esp. references)
	//  where something fails?
	for( i = 0 ; i < filekeys.count ; i++ ) {
		r = trackedpackage_addfilekey(tracks, pkg,
				ft_SOURCE, filekeys.values[i],
				true, database);
		filekeys.values[i] = NULL;
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&filekeys);
			trackedpackage_free(pkg);
			return r;
		}
	}
	strlist_done(&filekeys);
	return tracking_save(tracks, pkg);
}

retvalue sources_getsourceandversion(UNUSED(struct target *t),const char *chunk,const char *packagename,char **source,char **version) {
	retvalue r;
	char *sourceversion;
	char *sourcename;

	//TODO: elliminate duplicate code!
	assert(packagename!=NULL);

	r = chunk_getvalue(chunk,"Version",&sourceversion);
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Missing 'Version' field in chunk:'%s'\n", chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	sourcename = strdup(packagename);
	if( sourcename == NULL ) {
		free(sourceversion);
		return RET_ERROR_OOM;
	}
	*source = sourcename;
	*version = sourceversion;
	return RET_OK;
}

/****************************************************************/

static inline retvalue getvalue(const char *filename,const char *chunk,const char *field,char **value) {
	retvalue r;

	r = chunk_getvalue(chunk,field,value);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing '%s' field in %s!\n",field,filename);
		r = RET_ERROR;
	}
	return r;
}

static inline retvalue checkvalue(const char *filename,const char *chunk,const char *field) {
	retvalue r;

	r = chunk_checkfield(chunk,field);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Cannot find '%s' field in %s!\n",field,filename);
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

retvalue sources_readdsc(struct dsc_headers *dsc, const char *filename, const char *filenametoshow, bool *broken) {
	retvalue r;
	struct strlist filelines;

	r = signature_readsignedchunk(filename, filenametoshow,
			&dsc->control, NULL, NULL, broken);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	if( verbose > 100 ) {
		fprintf(stderr, "Extracted control chunk from '%s': '%s'\n",
				filenametoshow, dsc->control);
	}

	/* first look for fields that should be there */

	r = chunk_getname(dsc->control, "Source", &dsc->name, false);
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Missing 'Source' field in %s!\n", filenametoshow);
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;

	/* This is needed and cannot be ignored unless 
	 * sources_complete is changed to not need it */
	r = checkvalue(filenametoshow, dsc->control, "Format");
	if( RET_WAS_ERROR(r) )
		return r;

	r = checkvalue(filenametoshow, dsc->control, "Maintainer");
	if( RET_WAS_ERROR(r) )
		return r;

	/* only recommended, so ignore errors with this: */
	(void) checkvalue(filenametoshow, dsc->control, "Standards-Version");

	r = getvalue(filenametoshow, dsc->control, "Version", &dsc->version);
	if( RET_WAS_ERROR(r) )
		return r;

	r = getvalue_n(dsc->control, SECTION_FIELDNAME, &dsc->section);
	if( RET_WAS_ERROR(r) )
		return r;
	r = getvalue_n(dsc->control, PRIORITY_FIELDNAME, &dsc->priority);
	if( RET_WAS_ERROR(r) )
		return r;

	r = chunk_getextralinelist(dsc->control, "Files", &filelines);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Files' field in %s!\n", filenametoshow);
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	r = checksumsarray_parse(&dsc->files, &filelines, filenametoshow);
	strlist_done(&filelines);
	return r;
}

void sources_done(struct dsc_headers *dsc) {
	free(dsc->name);
	free(dsc->version);
	free(dsc->control);
	checksumsarray_done(&dsc->files);
	free(dsc->section);
	free(dsc->priority);
}

retvalue sources_complete(const struct dsc_headers *dsc, const char *directory, const struct overrideinfo *override, const char *section, const char *priority, char **newcontrol) {
	retvalue r;
	struct fieldtoadd *name;
	struct fieldtoadd *replace;
	char *newchunk,*newchunk2;
	char *newfilelines;

	assert(section != NULL && priority != NULL);

	/* first replace the "Source" with a "Package": */
	name = addfield_new("Package",dsc->name,NULL);
	if( name == NULL )
		return RET_ERROR_OOM;
	name = deletefield_new("Source",name);
	if( name == NULL )
		return RET_ERROR_OOM;
	newchunk2  = chunk_replacefields(dsc->control,name,"Format");
	addfield_free(name);
	if( newchunk2 == NULL )
		return RET_ERROR_OOM;

	r = sources_calcfilelines(&dsc->files, &newfilelines);
	if( RET_WAS_ERROR(r) ) {
		free(newchunk2);
		return RET_ERROR_OOM;
	}
	replace = addfield_new("Files",newfilelines,NULL);
	if( replace != NULL )
		replace = addfield_new("Directory",directory,replace);
	if( replace != NULL )
		replace = deletefield_new("Status",replace);
	if( replace != NULL )
		replace = addfield_new(SECTION_FIELDNAME,section,replace);
	if( replace != NULL )
		replace = addfield_new(PRIORITY_FIELDNAME,priority,replace);
	if( replace != NULL )
		replace = override_addreplacefields(override,replace);
	if( replace == NULL ) {
		free(newfilelines);
		free(newchunk2);
		return RET_ERROR_OOM;
	}

	newchunk  = chunk_replacefields(newchunk2,replace,"Files");
	free(newfilelines);
	free(newchunk2);
	addfield_free(replace);
	if( newchunk == NULL ) {
		return RET_ERROR_OOM;
	}

	*newcontrol = newchunk;

	return RET_OK;
}

