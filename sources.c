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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include <config.h>

#include <assert.h>
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

static retvalue getBasenamesAndMd5(const struct strlist *filelines,/*@out@*/struct strlist *basenames,/*@out@*/struct strlist *md5sums) {
	int i;
	retvalue r;

	assert( filelines != NULL && basenames != NULL && md5sums != NULL );

	r = strlist_init_n(filelines->count,basenames);
	if( RET_WAS_ERROR(r) )
		return r;
	r = strlist_init_n(filelines->count,md5sums);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(basenames);
		return r;
	}
	r = RET_NOTHING;
	for( i = 0 ; i < filelines->count ; i++ ) {
		char *basename,*md5sum;
		const char *fileline=filelines->values[i];

		r = calc_parsefileline(fileline,&basename,&md5sum);
		if( RET_WAS_ERROR(r) )
			break;

		r = strlist_add(md5sums,md5sum);
		if( RET_WAS_ERROR(r) ) {
			free(basename);
			break;
		}
		r = strlist_add(basenames,basename);
		if( RET_WAS_ERROR(r) ) {
			break;
		}
		r = RET_OK;
	}
	if( RET_WAS_ERROR(r) ) {
		strlist_done(basenames);
		strlist_done(md5sums);
	} else {
		assert( filelines->count == basenames->count );
		assert( filelines->count == md5sums->count );
	}
	return r;
}

/* get the intresting information out of a "Sources.gz"-chunk */
static retvalue parse_chunk(const char *chunk,/*@out@*/char **origdirectory,/*@out@*/struct strlist *basefiles,/*@out@*/struct strlist *md5sums) {
	retvalue r;
	char *od;

	if( origdirectory != NULL ) {
		/* Read the directory given there */
		r = chunk_getvalue(chunk,"Directory",&od);
		if( !RET_IS_OK(r) ) {
			return r;
		}
		if( verbose > 33 ) 
			fprintf(stderr,"got: %s\n",od);
	}


	/* collect the given md5sum and size */

  	if( basefiles != NULL ) {
		struct strlist filelines;
  
		r = chunk_getextralinelist(chunk,"Files",&filelines);
		if( !RET_IS_OK(r) ) {
			if( origdirectory != NULL )
				free(od);
  			return r;
		}
		if( md5sums != NULL )
			r = getBasenamesAndMd5(&filelines,basefiles,md5sums);
		else
			r = getBasenames(&filelines,basefiles);
		strlist_done(&filelines);
		if( RET_WAS_ERROR(r) ) {
			if( origdirectory != NULL )
				free(od);
  			return r;
		}
	} else 
		assert(md5sums == NULL); /* only together with basefiles */

	if( origdirectory != NULL )
		*origdirectory = od;
	return RET_OK;
}

static inline retvalue calcnewcontrol(
		const char *chunk, const char *package,
		const struct strlist *basenames,
		const char *component,const char *origdirectory,
		/*@out@*/struct strlist *filekeys,/*@out@*/char **newchunk,/*@out@*/struct strlist *origfiles) {
	char *directory;
	retvalue r;

	r = propersourcename(package);
	assert( r != RET_NOTHING );
	if( RET_IS_OK(r) )
		r = properfilenames(basenames);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Forbidden characters in source package '%s'!\n",package);
		return r;
	}

	directory = calc_sourcedir(component,package);
	if( directory == NULL ) 
		return RET_ERROR_OOM;
	
	r = calc_dirconcats(directory,basenames,filekeys);
	if( RET_WAS_ERROR(r) ) {
		free(directory);
		return r;
	}
	*newchunk = chunk_replacefield(chunk,"Directory",directory);
	free(directory);
	if( newchunk == NULL ) {
		strlist_done(filekeys);
		return RET_ERROR_OOM;
	}
	r = calc_dirconcats(origdirectory,basenames,origfiles);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(filekeys);
		free(*newchunk);
		return r;
	}
	return RET_OK;
}

/* Get the files and their expected md5sums */
retvalue sources_parse_getmd5sums(const char *chunk,struct strlist *basenames, struct strlist *md5sums) {
	retvalue r;

	r = parse_chunk(chunk,NULL,basenames,md5sums);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Does not look like source control: '%s'\n",chunk);
		return RET_ERROR;
	}
	return r;
}
	
retvalue sources_calcfilelines(const struct strlist *basenames,const struct strlist *md5sums,char **item) {
	size_t len;
	int i;
	char *result;

	assert( basenames != NULL && md5sums != NULL && basenames->count == md5sums->count );

	len = 1;
	for( i=0 ; i < basenames->count ; i++ ) {
		len += 3+strlen(basenames->values[i])+strlen(md5sums->values[i]);
	}
	result = malloc(len*sizeof(char));
	if( result == NULL )
		return RET_ERROR_OOM;
	*item = result;
	*(result++) = '\n';
	for( i=0 ; i < basenames->count ; i++ ) {
		*(result++) = ' ';
		strcpy(result,md5sums->values[i]);
		result += strlen(md5sums->values[i]);
		*(result++) = ' ';
		strcpy(result,basenames->values[i]);
		result += strlen(basenames->values[i]);
		*(result++) = '\n';
	}
	*(--result) = '\0';
	return RET_OK;
}

retvalue sources_getname(UNUSED(struct target *t),const char *control,char **packagename){
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
retvalue sources_getversion(UNUSED(struct target *t),const char *control,char **version) {
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
	
retvalue sources_getinstalldata(struct target *t,const char *packagename,UNUSED(const char *version),const char *chunk,char **control,struct strlist *filekeys,struct strlist *md5sums,struct strlist *origfiles) {
	retvalue r;
	char *origdirectory;
	struct strlist filelines,basenames;

	r = chunk_getextralinelist(chunk,"Files",&filelines);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Files' entry in '%s'!\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
  		return r;
	r = chunk_getvalue(chunk,"Directory",&origdirectory);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing 'Directory' entry in '%s'!\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	r = getBasenamesAndMd5(&filelines,&basenames,md5sums);
	strlist_done(&filelines);
	if( RET_WAS_ERROR(r) ) {
		free(origdirectory);
		return r;
	}
	r = properfilenames(&basenames);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		free(origdirectory);
		strlist_done(&basenames);
		strlist_done(md5sums);
		return r;
	}
	r = calcnewcontrol(chunk,packagename,&basenames,t->component,
			origdirectory,filekeys,control,origfiles);
	free(origdirectory);
	strlist_done(&basenames);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(md5sums);
	}
	return r;
}

retvalue sources_getfilekeys(UNUSED(struct target *t),const char *chunk,struct strlist *filekeys,/*@null@*/struct strlist *md5sums) {
	char *origdirectory;
	struct strlist basenames,mymd5sums;
	retvalue r;
	
	if( md5sums != NULL )
		r = parse_chunk(chunk,&origdirectory,&basenames,&mymd5sums);
	else
		r = parse_chunk(chunk,&origdirectory,&basenames,NULL);
	if( r == RET_NOTHING ) {
		//TODO: check if it is even text and do not print 
		//of looking binary??
		fprintf(stderr,"Does not look like source control: '%s'\n",chunk);
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	assert( RET_IS_OK(r) );

	r = calc_dirconcats(origdirectory,&basenames,filekeys);
	free(origdirectory);
	strlist_done(&basenames);
	if( RET_WAS_ERROR(r) ) {
		if( md5sums != NULL )
			strlist_done(&mymd5sums);
		return r;
	}
	if( md5sums != NULL )
		strlist_move(md5sums,&mymd5sums);
	return r;
}

char *sources_getupstreamindex(UNUSED(struct target *target),const char *suite_from,
		const char *component_from,UNUSED(const char *architecture)) {
	return mprintf("dists/%s/%s/source/Sources.gz",suite_from,component_from);
}

retvalue sources_doreoverride(const struct alloverrides *ao,const char *packagename,const char *controlchunk,/*@out@*/char **newcontrolchunk) {
	const struct overrideinfo *o;
	struct fieldtoadd *fields;
	char *newchunk;

	o = override_search(ao->dsc, packagename);
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

retvalue sources_retrack(struct target *t,const char *sourcename,const char *chunk, trackingdb tracks,references refs) {
	retvalue r;
	char *sourceversion;
	struct trackedpackage *pkg;
	struct strlist filekeys;

	//TODO: elliminate duplicate code!
	assert(sourcename!=NULL);

	r = chunk_getvalue(chunk,"Version",&sourceversion);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find Version in chunk:'%s'\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	r = sources_getfilekeys(t,chunk,&filekeys,NULL);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Malformed source control:'%s'\n",chunk);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(sourceversion);
		return r;
	}

	r = tracking_get(tracks,sourcename,sourceversion,&pkg);
	if( RET_WAS_ERROR(r) ) {
		free(sourceversion);
		strlist_done(&filekeys);
		return r;
	}
	if( r == RET_NOTHING ) {
		r = tracking_new(tracks,sourcename,sourceversion,&pkg);
		free(sourceversion);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&filekeys);
			return r;
		}
		r = trackedpackage_addfilekeys(tracks,pkg,ft_SOURCE,&filekeys,TRUE,refs);
		strlist_done(&filekeys);
		if( RET_WAS_ERROR(r) )
			return r;
		r = tracking_put(tracks,pkg);
		trackedpackage_free(pkg);
		return r;
	}
	free(sourceversion);

	r = trackedpackage_addfilekeys(tracks,pkg,ft_SOURCE,&filekeys,TRUE,refs);
	strlist_done(&filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	r = tracking_replace(tracks,pkg);
	trackedpackage_free(pkg);
	return r;
}

retvalue sources_getsourceandversion(UNUSED(struct target *t),const char *chunk,const char *packagename,char **source,char **version) {
	retvalue r;
	char *sourceversion;
	char *sourcename;

	//TODO: elliminate duplicate code!
	assert(packagename!=NULL);

	r = chunk_getvalue(chunk,"Version",&sourceversion);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find Version in chunk:'%s'\n",chunk);
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
