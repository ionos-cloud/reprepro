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

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include "error.h"
#include "strlist.h"
#include "packages.h"
#include "reference.h"
#include "chunks.h"
#include "sources.h"
#include "names.h"
#include "dpkgversions.h"

extern int verbose;

/* traverse through a '\n' sepeated lit of "<md5sum> <size> <filename>" 
 * > 0 while entires found, ==0 when not, <0 on error */
retvalue sources_getfile(const char *fileline,char **basename,char **md5sum) {
	const char *md5,*md5end,*size,*sizeend,*fn,*fnend;
	char *md5as,*filen;

	assert( fileline != NULL );
	if( *fileline == '\0' )
		return RET_NOTHING;

	/* the md5sums begins after the (perhaps) heading spaces ...  */
	md5 = fileline;
	while( isspace(*md5) )
		md5++;
	if( *md5 == '\0' )
		return RET_NOTHING;

	/* ... and ends with the following spaces. */
	md5end = md5;
	while( *md5end != '\0' && !isspace(*md5end) )
		md5end++;
	if( !isspace(*md5end) ) {
		if( verbose >= 0 ) {
			fprintf(stderr,"Expecting more data after md5sum!\n");
		}
		return RET_ERROR;
	}
	/* Then the size of the file is expected: */
	size = md5end;
	while( isspace(*size) )
		size++;
	sizeend = size;
	while( isdigit(*sizeend) )
		sizeend++;
	if( !isspace(*sizeend) ) {
		if( verbose >= 0 ) {
			fprintf(stderr,"Error in parsing size or missing space afterwards!\n");
		}
		return RET_ERROR;
	}
	/* Then the filename */
	fn = sizeend;
	while( isspace(*fn) )
		fn++;
	fnend = fn;
	while( *fnend != '\0' && !isspace(*fnend) )
		fnend++;

	filen = strndup(fn,fnend-fn);
	if( !filen )
		return RET_ERROR_OOM;
	if( md5sum ) {
		md5as = malloc((md5end-md5)+2+(sizeend-size));
		if( !md5as ) {
			free(filen);
			return RET_ERROR_OOM;
		}
		strncpy(md5as,md5,md5end-md5);
		md5as[md5end-md5] = ' ';
		strncpy(md5as+1+(md5end-md5),size,sizeend-size);
		md5as[(md5end-md5)+1+(sizeend-size)] = '\0';
	
		*md5sum = md5as;
	}
	if( basename )
		*basename = filen;
	else
		free(filen);

//	fprintf(stderr,"'%s' -> '%s' \n",*filename,*md5sum);
	
	return RET_OK;
}

static retvalue getBasenames(const struct strlist *filelines,struct strlist *basenames) {
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

		r = sources_getfile(fileline,&basename,NULL);
		if( RET_WAS_ERROR(r) )
			break;

		r = strlist_add(basenames,basename);
		if( RET_WAS_ERROR(r) ) {
			free(basename);
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

static retvalue getBasenamesAndMd5(const struct strlist *filelines,struct strlist *basenames,struct strlist *md5sums) {
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

		r = sources_getfile(fileline,&basename,&md5sum);
		if( RET_WAS_ERROR(r) )
			break;

		r = strlist_add(md5sums,md5sum);
		if( RET_WAS_ERROR(r) ) {
			free(md5sum);
			free(basename);
			break;
		}
		r = strlist_add(basenames,basename);
		if( RET_WAS_ERROR(r) ) {
			free(basename);
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
static retvalue sources_parse_chunk(const char *chunk,char **packagename,char **version,char **origdirectory,struct strlist *basefiles,struct strlist *md5sums) {
	retvalue r;
#define IFREE(p) if(p) free(*p);

	if( packagename ) {
		r = chunk_getname(chunk,"Package",packagename,0);
		if( !RET_IS_OK(r) )
			return r;
	}
	
	if( version ) {
		r = chunk_getvalue(chunk,"Version",version);
		if( !RET_IS_OK(r) ) {
			IFREE(packagename);
			return r;
		}
	}

	if( origdirectory ) {
		/* Read the directory given there */
		r = chunk_getvalue(chunk,"Directory",origdirectory);
		if( !RET_IS_OK(r) ) {
			IFREE(packagename);
			IFREE(version);
			return r;
		}
		if( verbose > 13 ) 
			fprintf(stderr,"got: %s\n",*origdirectory);
	}


	/* collect the given md5sum and size */

  	if( basefiles ) {
		struct strlist filelines;
  
		r = chunk_getextralinelist(chunk,"Files",&filelines);
		if( !RET_IS_OK(r) ) {
			IFREE(packagename);
			IFREE(version);
  			IFREE(origdirectory);
  			return r;
		}
		if( md5sums )
			r = getBasenamesAndMd5(&filelines,basefiles,md5sums);
		else
			r = getBasenames(&filelines,basefiles);
		strlist_done(&filelines);
		if( RET_WAS_ERROR(r) ) {
			IFREE(packagename);
			IFREE(version);
  			IFREE(origdirectory);
  			return r;
		}
	} else 
		assert(md5sums == NULL); /* only together with basefiles */

	return RET_OK;
}

retvalue sources_parse_getfilekeys(const char *chunk, struct strlist *filekeys) {
	char *origdirectory;
	struct strlist basenames;
	retvalue r;
	
	r = sources_parse_chunk(chunk,NULL,NULL,&origdirectory,&basenames,NULL);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Does not look like source control: '%s'\n",chunk);
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;
	assert( RET_IS_OK(r) );

	r = calc_dirconcats(origdirectory,&basenames,filekeys);
	free(origdirectory);
	strlist_done(&basenames);
	if( RET_WAS_ERROR(r) )
		return r;
	return r;
}

/* Look for an older version of the Package in the database.
 * return RET_NOTHING, if there is none at all. */
retvalue sources_lookforold(
		packagesdb packages,const char *packagename,
		struct strlist *oldfiles) {
	char *oldchunk;
	retvalue r;

	r = packages_get(packages,packagename,&oldchunk);
	if( !RET_IS_OK(r) ) {
		return r;
	}
	r = sources_parse_getfilekeys(oldchunk,oldfiles);
	free(oldchunk);
	if( RET_WAS_ERROR(r) )
		return r;

	return RET_OK;
}

/* Look for an older version of the Package in the database.
 * Set *oldversion, if there is already a newer (or equal) version to
 * <version>, return RET_NOTHING, if there is none at all. */
retvalue sources_lookforolder(
		packagesdb packages,const char *packagename,
		const char *newversion,char **oldversion,
		struct strlist *oldfiles) {
	char *oldchunk,*ov;
	retvalue r;

	assert(oldversion != NULL && newversion != NULL);

	r = packages_get(packages,packagename,&oldchunk);
	if( !RET_IS_OK(r) ) {
		return r;
	}

	r = sources_parse_chunk(oldchunk,NULL,&ov,NULL,NULL,NULL);

	if( !RET_IS_OK(r) ) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"Does not look like source control: '%s'\n",oldchunk);
			r = RET_ERROR;

		}
		free(oldchunk);
		return r;
	}

	r = dpkgversions_isNewer(newversion,ov);

	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Parse errors processing versions of %s.\n",packagename);
		free(ov);
		free(oldchunk);
		return r;
	}
	if( RET_IS_OK(r) ) {
		*oldversion = NULL;
		free(ov);
	} else
		*oldversion = ov;

	r = sources_parse_getfilekeys(oldchunk,oldfiles);
	free(oldchunk);
	if( RET_WAS_ERROR(r) )
		return r;

	return RET_OK;
}

static inline retvalue calcnewcontrol(
		const char *chunk, const char *package,
		const struct strlist *basenames,
		const char *component,const char *origdirectory,
		struct strlist *filekeys,char **newchunk,struct strlist *origfiles) {
	char *directory;
	retvalue r;

	directory =  calc_sourcedir(component,package);
	if( !directory ) 
		return RET_ERROR_OOM;
	
	r = calc_dirconcats(directory,basenames,filekeys);
	if( RET_WAS_ERROR(r) ) {
		free(directory);
		return r;
	}
	*newchunk = chunk_replacefield(chunk,"Directory",directory);
	free(directory);
	if( !newchunk ) {
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

static inline retvalue callaction(new_package_action *action, void *data,
		const char *chunk, const char *package, const char *version,
		const char *origdirectory, const struct strlist *basenames,
		const struct strlist *md5sums,
		const char *component, const struct strlist *oldfilekeys) {
	char *newchunk;
	struct strlist origfiles,filekeys;
	retvalue r;

	r = calcnewcontrol(chunk,package,basenames,component,origdirectory,&filekeys,&newchunk,&origfiles);
	if( RET_WAS_ERROR(r) )
		return r;
	

// Calculating origfiles and newchunk will both not be needed in half of the
// cases. This could be avoided by pushing flags to sources_findnew which
// to generete. (doing replace_field here makes handling in main.c so
// nicely type-independent.)

	r = (*action)(data,newchunk,package,version,
			&filekeys,&origfiles,md5sums,oldfilekeys);
	free(newchunk);
	strlist_done(&filekeys);
	strlist_done(&origfiles);

	return r;
}


//typedef retvalue source_package_action(void *data,const char *chunk,const char *package,const char *directory,const char *origdirectory,const char *files,const char *oldchunk);

struct sources_add {packagesdb pkgs; void *data; const char *component; new_package_action *action; };

static retvalue processsource(void *data,const char *chunk) {
	retvalue r;
	struct sources_add *d = data;

	char *package,*version,*origdirectory;
	char *oldversion;
	struct strlist basenames,md5sums,oldfilekeys;

	r = sources_parse_chunk(chunk,&package,&version,&origdirectory,&basenames,&md5sums);
	if( r == RET_NOTHING ) {
		// TODO: error?
		return RET_ERROR;
	} else if( RET_WAS_ERROR(r) ) {
		return r;
	}

	r = sources_lookforolder(d->pkgs,package,version,&oldversion,&oldfilekeys);
	if( r == RET_NOTHING )
		r = callaction(d->action,d->data,
				chunk,package,version,
				origdirectory,&basenames,&md5sums,
				d->component,NULL);
	else if( RET_IS_OK(r) ) {
		if( oldversion != NULL ) {
			if( verbose > 40 )
				fprintf(stderr,
"Ignoring '%s' with version '%s', as '%s' is already there.\n"
					,package,version,oldversion);
			free(oldversion);
			r = RET_NOTHING;
		} else 
			r = callaction(d->action,d->data,
					chunk,package,version,
					origdirectory,&basenames,&md5sums,
					d->component,&oldfilekeys);
		strlist_done(&oldfilekeys);
	}
	free(package);free(version);free(origdirectory);
	strlist_done(&basenames);strlist_done(&md5sums);
	return r;
}

/* call <data> for each package in the "Sources.gz"-style file <source_file> missing in
 * <pkgs> and using <component> as subdir of pool (i.e. "main","contrib",...) for generated paths */
retvalue sources_findnew(packagesdb pkgs,const char *component,const char *sources_file, new_package_action action,void *data,int force) {
	struct sources_add mydata;

	mydata.data=data;
	mydata.pkgs=pkgs;
	mydata.component=component;
	mydata.action=action;

	return chunk_foreach(sources_file,processsource,&mydata,force,0);
}

/* Get the files and their expected md5sums */
retvalue sources_parse_getmd5sums(const char *chunk,struct strlist *basenames, struct strlist *md5sums) {
	retvalue r;

	r = sources_parse_chunk(chunk,NULL,NULL,NULL,basenames,md5sums);
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
	if( !result )
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

retvalue sources_getname(target t,const char *control,char **packagename){
	retvalue r;

	r = chunk_getvalue(control,"Package",packagename);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not found Package name in chunk:'%s'\n",control);
		return RET_ERROR;
	}
	return r;
}
retvalue sources_getversion(target t,const char *control,char **version) {
	retvalue r;

	r = chunk_getvalue(control,"Version",version);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not found Version in chunk:'%s'\n",control);
		return RET_ERROR;
	}
	return r;
}
	
retvalue sources_getinstalldata(target t,const char *packagename,const char *version,const char *chunk,char **control,struct strlist *filekeys,struct strlist *md5sums,struct strlist *origfiles) {
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
	r = calcnewcontrol(chunk,packagename,&basenames,t->component,
			origdirectory,filekeys,control,origfiles);
	free(origdirectory);
	strlist_done(&basenames);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(md5sums);
	}
	return r;
}

retvalue sources_getfilekeys(target t,const char *name,const char *chunk,struct strlist *filekeys) {
	return sources_parse_getfilekeys(chunk,filekeys);
}
