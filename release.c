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
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "chunks.h"
#include "md5sum.h"
#include "copyfile.h"
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
	if( fi == NULL ) {
		fprintf(stderr,"Error opening %s: %m!\n",releasefile);
		return RET_ERRNO(errno);
	}
	r = chunk_read(fi,&chunk);
	i = gzclose(fi);
	if( !RET_IS_OK(r) ) {
		fprintf(stderr,"Error reading %s.\n",releasefile);
		if( r == RET_NOTHING )
			return RET_ERROR;
		else
			return r;
	}
	if( i < 0) {
		fprintf(stderr,"Closing revealed reading error in %s.\n",releasefile);
		free(chunk);
		return RET_ZERRNO(i);
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
retvalue release_genrelease(const char *distributiondir,const struct distribution *distribution,const struct target *target,const char *releasename,bool_t onlyifneeded, struct strlist *releasedfiles) {
	FILE *f;
	char *filename,*h;
	retvalue r;
	int e;

	filename = calc_dirconcat3(distributiondir,target->relativedirectory,releasename);
	if( filename == NULL ) {
		return RET_ERROR_OOM;
	}
	if( onlyifneeded && isregularfile(filename) ) {
		free(filename);
		filename = calc_dirconcat(target->relativedirectory,releasename);
		r = strlist_add(releasedfiles,filename);
		if( RET_WAS_ERROR(r) )
			return r;
		return RET_NOTHING;
	}
	h = filename;
	filename = calc_addsuffix(h,"new");
	free(h);
	if( filename == NULL ) {
		return RET_ERROR_OOM;
	}

	(void)unlink(filename);
	f = fopen(filename,"w");
	if( f == NULL ) {
		e = errno;
		fprintf(stderr,"Error writing file %s: %m\n",filename);
		free(filename);
		return RET_ERRNO(e);
	}
	free(filename);

	// TODO: check all return codes...
	if( distribution->suite != NULL )
		fprintf(f,	"Archive: %s\n",distribution->suite);
	if( distribution->version != NULL )
		fprintf(f,	"Version: %s\n",distribution->version);
	fprintf(f,		"Component: %s\n",target->component);
	if( distribution->origin != NULL )
		fprintf(f,	"Origin: %s\n",distribution->origin);
	if( distribution->label != NULL )
		fprintf(f,	"Label: %s\n",distribution->label);
	fprintf(f,		"Architecture: %s\n",target->architecture);
	if( distribution->description != NULL )
		fprintf(f,	"Description: %s\n",distribution->description);

	if( fclose(f) != 0 )
		return RET_ERRNO(errno);

	filename = calc_dirsuffixconcat(target->relativedirectory,releasename,"new");
	if( filename == NULL )
		return RET_ERROR_OOM;
	r = strlist_add(releasedfiles,filename);
	if( RET_WAS_ERROR(r) )
		return r;

	return RET_OK;
	
}

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const char *dirofdist,const struct distribution *distribution,struct strlist *releasedfiles,int force) {
	FILE *f;
	char *filename;
	size_t s;
	int e;
	retvalue result,r;
	char buffer[100];
	time_t t;
	struct tm *gmt;
	int i;

	(void)time(&t);
	gmt = gmtime(&t);
	if( gmt == NULL )
		return RET_ERROR_OOM;
	// s=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S %z",localtime(&t));
	s=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S +0000",gmt);
	if( s == 0 || s == 99) {
		fprintf(stderr,"strftime is doing strange things...\n");
		return RET_ERROR;
	}

	filename = calc_dirconcat(dirofdist,"Release.new");
	if( filename == NULL ) {
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(filename);
	(void)unlink(filename);
	f = fopen(filename,"w");
	if( f == NULL ) {
		e = errno;
		fprintf(stderr,"Error writing file %s: %m\n",filename);
		free(filename);
		return RET_ERRNO(e);
	}
#define checkwritten if( e < 0 ) { \
		e = errno; \
		fprintf(stderr,"Error writing to %s: %d=$m!\n",filename,e); \
		free(filename); \
		(void)fclose(f); \
		return RET_ERRNO(e); \
	}

	if( distribution->origin != NULL ) {
		e = fputs("Origin: ",f);
		checkwritten;
		e = fputs(distribution->origin,f);
		checkwritten;
		e = fputc('\n',f) - 1;
		checkwritten;
	}
	if( distribution->label != NULL ) {
		e = fputs("Label: ",f);
		checkwritten;
		e = fputs(distribution->label,f);
		checkwritten;
		e = fputc('\n',f) - 1;
		checkwritten;
	}
	if( distribution->suite != NULL ) {
		e = fputs("Suite: ",f);
		checkwritten;
		e = fputs(distribution->suite,f);
		checkwritten;
		e = fputc('\n',f) - 1;
		checkwritten;
	}
	e = fputs("Codename: ",f);
	checkwritten;
	e = fputs(distribution->codename,f);
	checkwritten;
	if( distribution->version != NULL ) {
		e = fputs("\nVersion: ",f);
		checkwritten;
		e = fputs(distribution->version,f);
		checkwritten;
	}
	e = fputs("\nDate: ",f);
	checkwritten;
	e = fputs(buffer,f);
	checkwritten;
	e = fputs("\nArchitectures:",f);
	checkwritten;
	for( i = 0 ; i < distribution->architectures.count ; i++ ) {
		/* Debian's topmost Release files do not list it, so we won't either */
		if( strcmp(distribution->architectures.values[i],"source") == 0 )
			continue;
		e = fputc(' ',f) - 1;
		checkwritten;
		e = fputs(distribution->architectures.values[i],f);
		checkwritten;
	}
	e = fputs("\nComponents: ",f);
	checkwritten;
	r = strlist_fprint(f,&distribution->components);
	if( RET_WAS_ERROR(r) ) {
		fprintf(stderr,"Error writing to %s!\n",filename);
		free(filename);
		(void)fclose(f);
		return r;
	}
	if( distribution->description != NULL ) {
		e = fputs("\nDescription: ",f);
		checkwritten;
		e = fputs(distribution->description,f);
		checkwritten;
	}

	e = fputc('\n',f);
	checkwritten;

	result = export_checksums(dirofdist,f,releasedfiles,force);

	e = fclose(f);
	checkwritten;
#undef checkwritten

	if( distribution->signwith != NULL ) { 
		char *newfilename;

		newfilename = calc_dirconcat(dirofdist,"Release.gpg.new");
		if( newfilename == NULL ) {
			free(filename);
			return RET_ERROR_OOM;
		}

		r = signature_sign(distribution->signwith,filename,newfilename);
		RET_UPDATE(result,r);
		free(newfilename);
	}
	free(filename);
	if( !RET_WAS_ERROR(result) || force > 0 ) {

		r = export_finalize(dirofdist,releasedfiles,force,distribution->signwith!=NULL);
		RET_UPDATE(result,r);
	}

	return result;
}

