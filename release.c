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
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>
#include <db.h>
#include "error.h"
#include "mprintf.h"
#include "chunks.h"
#include "sources.h"
#include "md5sum.h"
#include "dirs.h"
#include "names.h"
#include "release.h"

extern int verbose;

/* check for a <filetocheck> to be have same md5sum and size as <nametocheck> in <releasefile>,
 * returns 1 if ok, == 0 if <nametocheck> not specified, != 1 on error */
retvalue release_checkfile(const char *releasefile,const char *nametocheck,const char *filetocheck) {
	retvalue r;
	gzFile fi;
	const char *f,*n;
	char *c,*files;
	char *filename,*md5andsize,*realmd5andsize;

	/* Get the md5sums from the Release-file */
	
	fi = gzopen(releasefile,"r");
	if( !fi ) {
		fprintf(stderr,"Error opening %s: %m!\n",releasefile);
		return RET_ERRNO(errno);
	}
	c = chunk_read(fi);
	gzclose(fi);
	if( !c ) {
		fprintf(stderr,"Error reading %s.\n",releasefile);
		return RET_ERROR;
	}
	f = chunk_getfield("MD5Sum",c);
	if( !f ){
		fprintf(stderr,"Missing MD5Sums-field.\n");
		free(c);
		return RET_ERROR;
	}
	files = chunk_dupextralines(f);
	free(c);
	if( !files )
		return RET_ERROR;
	
	realmd5andsize = NULL;
	if( ! RET_IS_OK(r=md5sum_and_size(&realmd5andsize,filetocheck,0))) {
		fprintf(stderr,"Error checking %s: %m\n",filetocheck);
		free(files);
		return r;
	}

	n = files;
	while( RET_IS_OK( r = sources_getfile(&n,&filename,&md5andsize) ) ) {
		if( verbose > 2 ) 
			fprintf(stderr,"is it %s?\n",filename);
		if( strcmp(filename,nametocheck) == 0 ) {
			if( verbose > 1 ) 
				fprintf(stderr,"found. is '%s' == '%s'?\n",md5andsize,realmd5andsize);
			if( strcmp(md5andsize,realmd5andsize) == 0 ) {
				if( verbose > 0 )
					printf("%s ok\n",nametocheck);
				free(md5andsize);free(filename);
				r = RET_OK;
				break;
			} else {
				if( verbose >=0 )
					fprintf(stderr,"%s failed\n",nametocheck);
				free(md5andsize);free(filename);
				r = RET_ERROR_WRONG_MD5;
				break;
			}
		}
		free(md5andsize);free(filename);
	}
	if( r==RET_NOTHING && verbose>=0 )
		fprintf(stderr,"%s failed as missing\n",nametocheck);
	free(realmd5andsize);
	free(files);
	return r;
}


void release_free(struct release *release) {
	if( release) {
		free(release->codename);
		free(release->suite);
		free(release->version);
		free(release->origin);
		free(release->label);
		free(release->description);
		free(release->architectures);
		free(release->components);
		free(release);
	}
}

retvalue release_parse(struct release **release,const char *chunk) {
	struct release *r;
	const char *f;

	assert( chunk && release );
	
	r = calloc(1,sizeof(struct release));
	if( !r )
		return RET_ERROR_OOM;

#define parse(name,target) 	f = chunk_getfield(name,chunk); \
				if( !f ) { \
					*release = NULL; \
					release_free(r); \
					return RET_NOTHING; \
				} \
				target = chunk_dupvalue(f); \
				if( !target ) { \
					release_free(r); \
					return RET_ERROR_OOM; \
				}
	parse("Codename",r->codename);
	parse("Suite",r->suite);
	parse("Version",r->version);
	parse("Origin",r->origin);
	parse("Label",r->label);
	parse("Description",r->description);
	parse("Architectures",r->architectures);
	parse("Components",r->components);

	*release = r;
	return RET_OK;
}

retvalue release_parse_and_filter(struct release **release,const char *chunk,struct release_filter filter) {
	retvalue result;
	int i;
	
	result = release_parse(release,chunk);
	if( RET_IS_OK(result) ) {
		if( filter.count > 0 ) {
			i = filter.count;
			while( i-- > 0 && strcmp((filter.dists)[i],(*release)->codename) != 0 ) {
			}
			if( i < 0 ) {
				if( verbose > 2 ) {
					fprintf(stderr,"skipping %s\n",(*release)->codename);
				}
				free(release);
				return RET_NOTHING;
			}
		}
	}
	return result;
}
	


/* Generate a "Release"-file for binary directory */
retvalue release_genbinary(const struct release *release,const char *arch,const char *component,const char *distdir) {
	FILE *f;
	char *filename;
	int e;


	filename = mprintf("%s/%s/%s/binary-%s/Release",distdir,release->codename,component,arch);
	if( !filename ) {
		return RET_ERROR_OOM;
	}
	make_parent_dirs(filename);
	f = fopen(filename,"w");
	if( !f ) {
		e = errno;
		fprintf(stderr,"Error rewriting file %s: %m\n",filename);
		free(filename);
		return RET_ERRNO(e);
	}
	free(filename);

	fprintf(f,	"Archive: %s\n"
			"Version: %s\n"
			"Component: %s\n"
			"Origin: %s\n"
			"Label: %s\n"
			"Architecture: %s\n"
			"Description: %s\n",
		release->suite,release->version,component,
		release->origin,release->label,arch,
		release->description);

	if( fclose(f) != 0 )
		return RET_ERRNO(errno);

	return RET_OK;
	
}

/* Generate a "Release"-file for source directory */
retvalue release_gensource(const struct release *release,const char *component,const char *distdir) {
	FILE *f;
	char *filename;
	int e;


	filename = mprintf("%s/%s/%s/source/Release",distdir,release->codename,component);
	if( !filename ) {
		return RET_ERROR_OOM;
	}
	make_parent_dirs(filename);
	f = fopen(filename,"w");
	if( !f ) {
		e = errno;
		fprintf(stderr,"Error rewriting file %s: %m\n",filename);
		free(filename);
		return RET_ERRNO(e);
	}
	free(filename);

	fprintf(f,	"Archive: %s\n"
			"Version: %s\n"
			"Component: %s\n"
			"Origin: %s\n"
			"Label: %s\n"
			"Architecture: source\n"
			"Description: %s\n",
		release->suite,release->version,component,
		release->origin,release->label,
		release->description);

	if( fclose(f) != 0 )
		return RET_ERRNO(errno);

	return RET_OK;
	
}

static retvalue printmd5andsize(FILE *f,const char *dir,const char *fmt,...) __attribute__ ((format (printf, 3, 4))); 
static retvalue printmd5andsize(FILE *f,const char *dir,const char *fmt,...) {
	va_list ap;
	char *fn,*filename,*md;
	retvalue r;

	va_start(ap,fmt);
	fn = vmprintf(fmt,ap);
	va_end(ap);
	if( !fn )
		return RET_ERROR_OOM;

	filename = calc_dirconcat(dir,fn);
	if( !filename ) {
		free(filename);
		return RET_ERROR_OOM;
	}
	
	r = md5sum_and_size(&md,filename,0);
	free(filename);

	if( !RET_IS_OK(r) ) {
		fprintf(stderr,"Error processing %s/%s\n",dir,fn);
		free(fn);
		return r;
	}

	fprintf(f," %s %s\n",md,fn);
	free(md);free(fn);

	return RET_OK;
}

static char *first_word(const char *c) {
	const char *p;

	p = c;
	while( *p && !isspace(*p) ) {
		p++;
	}
	return strndup(c,p-c);
}

static const char *next_word(const char *c) {

	while( *c && !isspace(*c) )
		c++;
	while( *c && isspace(*c) )
		c++;
	return c;
}

/* call <sourceaction> for each source part of <release> and <binaction> for each binary part of it. */
retvalue release_foreach_part(const struct release *release,release_each_source_action sourceaction,release_each_binary_action binaction,void *data) {
	retvalue result,r;
	const char *arch,*comp;
	char *a,*c;

	result = RET_NOTHING;
	for( comp=release->components ; *comp ; comp=next_word(comp) ) {
		c = first_word(comp);
		if( !c ) {return RET_ERROR_OOM;}
		for( arch=release->architectures ; *arch ; arch=next_word(arch) ) {
			a = first_word(arch);
			if( !a ) {free(c);return RET_ERROR_OOM;}
			if( strcmp(a,"source") != 0 ) {
				r = binaction(data,c,a);
				RET_UPDATE(result,r);
				//TODO: decide if break on error/introduce a force-flag
			}
			free(a);
			
		}
		r = sourceaction(data,c);
		RET_UPDATE(result,r);
		//TODO: dito
		free(c);
	}
	return result;
}

struct genrel { FILE *f; const char *dirofdist; };

static retvalue printbin(void *data,const char *component,const char *architecture) {
	retvalue result,r;
	struct genrel *d = data;

	result = printmd5andsize(d->f,d->dirofdist,
		"%s/binary-%s/Release",
		component,architecture);

	r = printmd5andsize(d->f,d->dirofdist,
		"%s/binary-%s/Packages",
		component,architecture);
	RET_UPDATE(result,r);
	r = printmd5andsize(d->f,d->dirofdist,
		"%s/binary-%s/Packages.gz",
		component,architecture);
	RET_UPDATE(result,r);

	return result;
}

static retvalue printsource(void *data,const char *component) {
	retvalue result,r;
	struct genrel *d = data;

	result = printmd5andsize(d->f,d->dirofdist,
			"%s/source/Release",
			component);
	r = printmd5andsize(d->f,d->dirofdist,
			"%s/source/Sources.gz",
			component);
	RET_UPDATE(result,r);

	return result;
}

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const struct release *release,const char *distdir) {
	FILE *f;
	char *filename;
	char *dirofdist;
	int e;
	retvalue result;
	char buffer[100];
	time_t t;

	struct genrel data;

	time(&t);
	// e=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S %z",localtime(&t));
	e=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S +0000",gmtime(&t));
	if( e == 0 || e == 99) {
		fprintf(stderr,"strftime is doing strange things...\n");
		return RET_ERROR;
	}

	dirofdist = calc_dirconcat(distdir,release->codename);
	if( !dirofdist ) {
		return RET_ERROR_OOM;
	}

	filename = calc_dirconcat(dirofdist,"Release");
	if( !filename ) {
		free(dirofdist);
		return RET_ERROR_OOM;
	}
	make_parent_dirs(filename);
	f = fopen(filename,"w");
	if( !f ) {
		e = errno;
		fprintf(stderr,"Error rewriting file %s: %m\n",filename);
		free(filename);
		free(dirofdist);
		return RET_ERRNO(e);
	}
	free(filename);

	fprintf(f,	
			"Origin: %s\n"
			"Label: %s\n"
			"Suite: %s\n"
			"Codename: %s\n"
			"Version: %s\n"
			"Date: %s\n"
			"Architectures: %s\n"
			"Components: %s\n"
			"Description: %s\n"
			"MD5Sums:\n",
		release->origin, release->label, release->suite,
		release->codename, release->version, buffer,
		release->architectures, release->components,
		release->description);

	/* generate bin/source-Release-files and add md5sums */

	data.f = f;
	data.dirofdist = dirofdist;
	result = release_foreach_part(release,printsource,printbin,&data);

	free(dirofdist);
	
	if( fclose(f) != 0 )
		return RET_ERRNO(errno);

	return result;
}

struct mydata {struct release_filter filter; releaseaction *action; void *data;};

static retvalue processrelease(void *d,const char *chunk) {
	struct mydata *mydata = d;
	retvalue result;
	struct release *release;

	result = release_parse_and_filter(&release,chunk,mydata->filter);
	if( RET_IS_OK(result) ){

		result = mydata->action(mydata->data,release);
		free(release);
	}

	return result;
}

retvalue release_foreach(const char *conf,int argc,char *argv[],releaseaction action,void *data,int force) {
	retvalue result;
	char *fn;
	struct mydata mydata;

	mydata.filter.count = argc;
	mydata.filter.dists = argv;
	mydata.data = data;
	mydata.action = action;
	
	fn = calc_dirconcat(conf,"distributions");
	if( !fn ) 
		return RET_ERROR_OOM;
	
	result = chunk_foreach(fn,processrelease,&mydata,force);

	free(fn);
	return result;
}
