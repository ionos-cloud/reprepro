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
#include "strlist.h"
#include "chunks.h"
#include "packages.h"
#include "sources.h"
#include "md5sum.h"
#include "dirs.h"
#include "names.h"
#include "signature.h"
#include "distribution.h"
#include "release.h"

extern int verbose;

// TODO: this file currently handles both, the checking of checksums in
// existing Release file and the creation of release files. Should this
// be in the same file or be split?

/* get a strlist with the md5sums of a Release-file */
retvalue release_getchecksums(const char *releasefile,struct strlist *info) {
	gzFile fi;
	retvalue r;
	char *chunk;
	struct strlist files;
	char *filename,*md5sum;
	int i;

	fi = gzopen(releasefile,"r");
	if( !fi ) {
		fprintf(stderr,"Error opening %s: %m!\n",releasefile);
		return RET_ERRNO(errno);
	}
	chunk = chunk_read(fi);
	//TODO: check returncode:
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

	r = strlist_init(info);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&files);
		return r;
	}

	for( i = 0 ; i < files.count ; i++ ) {
		r = sources_getfile(files.values[i],&filename,&md5sum);
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
		r = strlist_add(info,md5sum);
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
static retvalue release_searchchecksum(const struct strlist *fileinfos, const char *nametocheck, const char *expected) {
	int i;
	const char *filename,*md5sum;

	for( i = 0 ; i+1 < fileinfos->count ; i+=2 ) {
		filename = fileinfos->values[i];
		md5sum = fileinfos->values[i+1];
		if( verbose > 20 ) 
			fprintf(stderr,"is it %s?\n",filename);
		if( strcmp(filename,nametocheck) == 0 ) {
			if( verbose > 19 ) 
				fprintf(stderr,"found. is '%s' == '%s'?\n",md5sum,expected);
			if( strcmp(md5sum,expected) == 0 )
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
	char *realmd5sum;
	retvalue r;

	/* this does not really belong here, but makes live easier... */
	if( !nametocheck || !filename )
		return RET_ERROR_OOM;

	r=md5sum_read(filename,&realmd5sum);
	if( !RET_IS_OK(r)) {
		if( r == RET_NOTHING ) {
			fprintf(stderr,"Error opening %s!\n",filename);
			r = RET_ERROR;
		}
		return r;
	}
	r = release_searchchecksum(fileinfos,nametocheck,realmd5sum);
	free(realmd5sum);
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

/* Generate a "Release"-file for binary directory */
retvalue release_genbinary(const struct distribution *distribution,const char *arch,const char *component,const char *distdir) {
	FILE *f;
	char *filename;
	int e;


	filename = mprintf("%s/%s/%s/binary-%s/Release",distdir,distribution->codename,component,arch);
	if( !filename ) {
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(filename);
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
		distribution->suite,distribution->version,component,
		distribution->origin,distribution->label,arch,
		distribution->description);

	if( fclose(f) != 0 )
		return RET_ERRNO(errno);

	return RET_OK;
	
}

/* Generate a "Release"-file for source directory */
retvalue release_gensource(const struct distribution *distribution,const char *component,const char *distdir) {
	FILE *f;
	char *filename;
	int e;


	filename = mprintf("%s/%s/%s/source/Release",distdir,distribution->codename,component);
	if( !filename ) {
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(filename);
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
		distribution->suite,distribution->version,component,
		distribution->origin,distribution->label,
		distribution->description);

	if( fclose(f) != 0 )
		return RET_ERRNO(errno);

	return RET_OK;
	
}

static retvalue printmd5sum(FILE *f,const char *dir,const char *fmt,...) __attribute__ ((format (printf, 3, 4))); 
static retvalue printmd5sum(FILE *f,const char *dir,const char *fmt,...) {
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
	
	r = md5sum_read(filename,&md);
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

struct genrel { FILE *f; const char *dirofdist; };

static retvalue printbin(void *data,const char *component,const char *architecture) {
	retvalue result,r;
	struct genrel *d = data;

	result = printmd5sum(d->f,d->dirofdist,
		"%s/binary-%s/Release",
		component,architecture);

	r = printmd5sum(d->f,d->dirofdist,
		"%s/binary-%s/Packages",
		component,architecture);
	RET_UPDATE(result,r);
	r = printmd5sum(d->f,d->dirofdist,
		"%s/binary-%s/Packages.gz",
		component,architecture);
	RET_UPDATE(result,r);

	return result;
}

static retvalue printsource(void *data,const char *component) {
	retvalue result,r;
	struct genrel *d = data;

	result = printmd5sum(d->f,d->dirofdist,
			"%s/source/Release",
			component);
	r = printmd5sum(d->f,d->dirofdist,
			"%s/source/Sources.gz",
			component);
	RET_UPDATE(result,r);

	return result;
}

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const struct distribution *distribution,const char *distdir,const char *chunk) {
	FILE *f;
	char *filename;
	char *dirofdist;
	size_t e;
	retvalue result,r;
	char buffer[100];
	time_t t;
	struct tm *gmt;

	struct genrel data;

	(void)time(&t);
	gmt = gmtime(&t);
	if( gmt == NULL )
		return RET_ERROR_OOM;
	// e=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S %z",localtime(&t));
	e=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S +0000",gmt);
	if( e == 0 || e == 99) {
		fprintf(stderr,"strftime is doing strange things...\n");
		return RET_ERROR;
	}

	dirofdist = calc_dirconcat(distdir,distribution->codename);
	if( !dirofdist ) {
		return RET_ERROR_OOM;
	}

	filename = calc_dirconcat(dirofdist,"Release");
	if( !filename ) {
		free(dirofdist);
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(filename);
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
		distribution->origin, distribution->label, distribution->suite,
		distribution->codename, distribution->version, buffer);
	strlist_fprint(f,&distribution->architectures);
	fprintf(f,    "\nComponents: ");
	strlist_fprint(f,&distribution->components);
	fprintf(f,    "\nDescription: %s\n"
			"MD5Sum:\n",
		distribution->description);

	/* generate bin/source-Release-files and add md5sums */

	data.f = f;
	data.dirofdist = dirofdist;
	//TODO: get a force from above?
	result = distribution_foreach_part(distribution,printsource,printbin,&data,0);

	if( fclose(f) != 0 ) {
		free(dirofdist);
		free(filename);
		return RET_ERRNO(errno);
	}

	r = signature_sign(chunk,filename);
	RET_UPDATE(result,r);

	free(dirofdist);
	free(filename);
	

	return result;
}

