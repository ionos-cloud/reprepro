/*  This file is part of "reprepro"
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
	struct strlist files,checksuminfo;
	char *filename,*md5sum;
	int i;

	fi = gzopen(releasefile,"r");
	if( !fi ) {
		fprintf(stderr,"Error opening %s: %m!\n",releasefile);
		return RET_ERRNO(errno);
	}
	r = chunk_read(fi,&chunk);
	i = gzclose(fi);
	if( i < 0)
		return RET_ZERRNO(i);
	if( !RET_IS_OK(r) ) {
		fprintf(stderr,"Error reading %s.\n",releasefile);
		if( r == RET_NOTHING )
			return RET_ERROR;
		else
			return r;
	}
	r = chunk_getextralinelist(chunk,"MD5Sum",&files);
	free(chunk);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing MD5Sum-field.\n");
		return RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;

	r = strlist_init(&checksuminfo);
	if( RET_WAS_ERROR(r) ) {
		strlist_done(&files);
		return r;
	}

	for( i = 0 ; i < files.count ; i++ ) {
		r = calc_parsefileline(files.values[i],&filename,&md5sum);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(&checksuminfo);
			return r;
		}
		r = strlist_add(&checksuminfo,filename);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(&checksuminfo);
			return r;
		}
		r = strlist_add(&checksuminfo,md5sum);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&files);
			strlist_done(&checksuminfo);
			return r;
		}
	}
	strlist_done(&files);
	assert( checksuminfo.count % 2 == 0 );

	strlist_move(info,&checksuminfo);
	return RET_OK;
}

/* Generate a "Release"-file for arbitrary directory */
retvalue release_genrelease(const struct distribution *distribution,const struct target *target,const char *distdir) {
	FILE *f;
	char *filename;
	int e;


	filename = calc_dirconcat3(distdir,target->directory,"Release");
	if( !filename ) {
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(filename);
	f = fopen(filename,"w");
	if( !f ) {
		e = errno;
		fprintf(stderr,"Error (re)writing file %s: %m\n",filename);
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
		distribution->suite,distribution->version,target->component,
		distribution->origin,distribution->label,target->architecture,
		distribution->description);

	if( fclose(f) != 0 )
		return RET_ERRNO(errno);

	return RET_OK;
	
}

struct genrel { FILE *f; const char *distdir; int force; };

static retvalue printmd5(void *data,struct target *target) {
	struct genrel *d = data;

	return target_printmd5sums(target,d->distdir,d->f,d->force);
}

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const struct distribution *distribution,const char *distdir,int force) {
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
	free(dirofdist);
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
	data.distdir = distdir;
	data.force = force;
	result = distribution_foreach_part(distribution,NULL,NULL,printmd5,&data,force);

	if( fclose(f) != 0 ) {
		free(filename);
		return RET_ERRNO(errno);
	}

	if( RET_WAS_ERROR(result) ){
		free(filename);
		return result;
	}

	/* in case of error or nothing to do there is nothing to do... */
	if( distribution->signwith ) { 
		r = signature_sign(distribution->signwith,filename);
		RET_UPDATE(result,r);
	}
	free(filename);
	return result;
}

