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
#include <stdlib.h>
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

/* get a strlist with the md5sums of a Release-file */
retvalue release_getchecksums(const char *releasefile,struct strlist *info) {
	gzFile fi;
	retvalue r;
	char *chunk;
	struct strlist files;
	char *filename,*md5andsize;
	int i;

	fi = gzopen(releasefile,"r");
	if( !fi ) {
		fprintf(stderr,"Error opening %s: %m!\n",releasefile);
		return RET_ERRNO(errno);
	}
	chunk = chunk_read(fi);
	gzclose(fi);
	if( !chunk ) {
		fprintf(stderr,"Error reading %s.\n",releasefile);
		return RET_ERROR;
	}
	r = chunk_getextralinelist(chunk,"MD5Sum",&files);
	free(chunk);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing MD5Sum-field.\n");
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;

	strlist_init(info);
	for( i = 0 ; i < files.count ; i++ ) {
		r = sources_getfile(files.values[i],&filename,&md5andsize);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(info);
			return r;
		}
		r = strlist_add(info,filename);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(info);
			return r;
		}
		r = strlist_add(info,md5andsize);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(info);
			return r;
		}
	}
	strlist_done(&files);
	assert( info->count % 2 == 0 );

	return RET_OK;
}

/* check in fileinfo for <nametocheck> to have md5sum and size <expected> *
 * returns RET_OK if ok, == RET_NOTHING if not found, error otherwise     */
retvalue release_searchchecksum(const struct strlist *fileinfos, const char *nametocheck, const char *expected) {
	int i;
	const char *filename,*md5andsize;

	for( i = 0 ; i+1 < fileinfos->count ; i+=2 ) {
		filename = fileinfos->values[i];
		md5andsize = fileinfos->values[i+1];
		if( verbose > 20 ) 
			fprintf(stderr,"is it %s?\n",filename);
		if( strcmp(filename,nametocheck) == 0 ) {
			if( verbose > 19 ) 
				fprintf(stderr,"found. is '%s' == '%s'?\n",md5andsize,expected);
			if( strcmp(md5andsize,expected) == 0 )
				return RET_OK;
			else 
				return RET_ERROR_WRONG_MD5;
		}
	}
	return RET_NOTHING;
}

/* check in fileinfo for <nametocheck> to have md5sum and size of <filename> *
 * returns RET_OK if ok, error otherwise     */
retvalue release_check(const struct strlist *fileinfos, const char *nametocheck, const char *filename) {
	char *realmd5andsize;
	retvalue r;

	/* this does not really belong here, but makes live easier... */
	if( !nametocheck || !filename )
		return RET_ERROR_OOM;

	r=md5sum_and_size(&realmd5andsize,filename,0);
	if( !RET_IS_OK(r)) {
		if( r == RET_NOTHING )
			r = RET_ERROR;
		fprintf(stderr,"Error calculating checksum of %s: %m\n",filename);
		return r;
	}
	r = release_searchchecksum(fileinfos,nametocheck,realmd5andsize);
	free(realmd5andsize);
	if( r == RET_NOTHING ) {
		r = RET_ERROR;
		fprintf(stderr,"Can't find authenticity '%s' for '%s'\n",nametocheck,filename);
	} else if( !RET_IS_OK(r)) 
		fprintf(stderr,"Error checking authenticity of %s\n",filename);
	return r;
}

/* check for a <filetocheck> to be have same md5sum and size as <nametocheck> in <releasefile>,
 * returns 1 if ok, == 0 if <nametocheck> not specified, != 1 on error */
retvalue release_checkfile(const char *releasefile,const char *nametocheck,const char *filetocheck) {
	retvalue r;
	struct strlist files;

	/* Get the md5sums from the Release-file */
	r = release_getchecksums(releasefile,&files);
	if( !RET_IS_OK(r) )
		return r;
	
	r = release_check(&files,nametocheck,filetocheck);

	if( verbose >=0 ) {
		if( RET_IS_OK(r) ) 
			printf("%s ok\n",nametocheck);
		else if( r == RET_NOTHING )
			fprintf(stderr,"%s failed as missing\n",nametocheck);
		else // if( r == RET_ERROR_WRONG_MD5 )
			fprintf(stderr,"%s failed\n",nametocheck);
	}


	strlist_done(&files);
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
		strlist_done(&release->architectures);
		strlist_done(&release->components);
		free(release);
	}
}

retvalue release_parse(struct release **release,const char *chunk) {
	struct release *r;
	retvalue ret;

	assert( chunk && release );
	
	r = calloc(1,sizeof(struct release));
	if( !r )
		return RET_ERROR_OOM;

#define checkret		 if(!RET_IS_OK(ret)) { \
					release_free(r); \
					return ret; \
				}
	ret = chunk_getvalue(chunk,"Codename",&r->codename);
	checkret;
	ret = chunk_getvalue(chunk,"Suite",&r->suite);
	checkret;
	ret = chunk_getvalue(chunk,"Version",&r->version);
	checkret;
	ret = chunk_getvalue(chunk,"Origin",&r->origin);
	checkret;
	ret = chunk_getvalue(chunk,"Label",&r->label);
	checkret;
	ret = chunk_getvalue(chunk,"Description",&r->description);
	checkret;
	ret = chunk_getwordlist(chunk,"Architectures",&r->architectures);
	checkret;
	ret = chunk_getwordlist(chunk,"Components",&r->components);
	checkret;

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
				release_free(*release);
				*release = NULL;
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

/* call <sourceaction> for each source part of <release> and <binaction> for each binary part of it. */
retvalue release_foreach_part(const struct release *release,release_each_source_action sourceaction,release_each_binary_action binaction,void *data) {
	retvalue result,r;
	int i,j;
	const char *arch,*comp;

	result = RET_NOTHING;
	for( i = 0 ; i < release->components.count ; i++ ) {
		comp = release->components.values[i];
		for( j = 0 ; j < release->architectures.count ; j++ ) {
			arch = release->architectures.values[j];
			if( strcmp(arch,"source") != 0 ) {
				r = binaction(data,comp,arch);
				RET_UPDATE(result,r);
				//TODO: decide if break on error/introduce a force-flag
			}
			
		}
		r = sourceaction(data,comp);
		RET_UPDATE(result,r);
		//TODO: dito
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
retvalue release_gen(const struct release *release,const char *distdir,const char *chunk) {
	FILE *f;
	char *filename;
	char *sigfilename,*signwith,*signcommand;
	char *dirofdist;
	int e,ret;
	retvalue result,r;
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

	fprintf(f,	
			"Origin: %s\n"
			"Label: %s\n"
			"Suite: %s\n"
			"Codename: %s\n"
			"Version: %s\n"
			"Date: %s\n"
			"Architectures: ",
		release->origin, release->label, release->suite,
		release->codename, release->version, buffer);
	strlist_fprint(f,&release->architectures);
	fprintf(f,    "\nComponents: ");
	strlist_fprint(f,&release->components);
	fprintf(f,    "\nDescription: %s\n"
			"MD5Sum:\n",
		release->description);

	/* generate bin/source-Release-files and add md5sums */

	data.f = f;
	data.dirofdist = dirofdist;
	result = release_foreach_part(release,printsource,printbin,&data);

	if( fclose(f) != 0 ) {
		free(dirofdist);
		free(filename);
		return RET_ERRNO(errno);
	}

	r = chunk_getvalue(chunk,"SignWith",&signwith);
	if( RET_WAS_ERROR(r) ) {
		free(dirofdist);
		free(filename);
		return RET_ERRNO(errno);
	}

	if( r != RET_NOTHING ) {
		sigfilename = calc_dirconcat(dirofdist,"Release.gpg");
		if( !sigfilename ) {
			free(dirofdist);
			free(signwith);
			free(filename);
			return RET_ERROR_OOM;
		}
		signcommand = mprintf("%s %s %s",signwith,sigfilename,filename);
		if( !signcommand ) {
			free(dirofdist); free(filename);
			free(signwith); free(signcommand);
			return RET_ERROR_OOM;
		}

		ret = unlink(sigfilename);
		if( ret != 0 && errno != ENOENT ) {
			fprintf(stderr,"Could not remove '%s' to prepare replacement: %m\n",sigfilename);
			free(signcommand);
			free(dirofdist);
			free(filename);
			return RET_ERROR;
		}
		
		free(sigfilename);
		free(signwith);

		ret = system(signcommand);
		if( ret != 0 ) {
			fprintf(stderr,"Executing '%s' returned: %d\n",signcommand,ret);
			result = RET_ERROR;
		}

		free(signcommand);
	}
	
	free(dirofdist);
	free(filename);
	

	return result;
}

struct mydata {struct release_filter filter; releaseaction *action; void *data;};

static retvalue processrelease(void *d,const char *chunk) {
	struct mydata *mydata = d;
	retvalue result;
	struct release *release;

	result = release_parse_and_filter(&release,chunk,mydata->filter);
	if( RET_IS_OK(result) ){

		result = mydata->action(mydata->data,chunk,release);
		release_free(release);
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
