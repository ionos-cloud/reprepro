/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
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
#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>

#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "chunks.h"
#include "packages.h"
#include "exports.h"
#include "md5sum.h"
#include "copyfile.h"

extern int verbose;

static retvalue printout(void *data,UNUSED(const char *package),const char *chunk) {
	FILE *pf = data;
	size_t l;

	l = strlen(chunk);
	if( fwrite(chunk,l,1,pf) != 1 || fwrite("\n",1,1,pf) != 1 )
		return RET_ERROR;
	else {
		if( chunk[l-1] != '\n' )
			if( fwrite("\n",1,1,pf) != 1 )
				return RET_ERROR;
		return RET_OK;
	}
}

static retvalue zprintout(void *data,UNUSED(const char *package),const char *chunk) {
	gzFile pf = data;
	size_t l;

	l = strlen(chunk);
	if( gzwrite(pf,(const voidp)chunk,l) != l || gzwrite(pf,"\n",1) != 1 )
		return RET_ERROR;
	else {
		if( chunk[l-1] != '\n' )
			if( gzwrite(pf,"\n",1) != 1 )
				return RET_ERROR;
		return RET_OK;
	}
}

/* print the database to a "Packages" or "Sources" file */
static retvalue packagesdb_printout(packagesdb packagesdb,const char *filename) {
	retvalue ret;
	int r;
	FILE *pf;

	pf = fopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		return RET_ERRNO(errno);
	}
	ret = packages_foreach(packagesdb,printout,pf,0);
	r = fclose(pf);
	if( r != 0 )
		RET_ENDUPDATE(ret,RET_ERRNO(errno));
	/* Writing an empty file is also something done */
	if( ret == RET_NOTHING )
		return RET_OK;
	return ret;
}

/* print the database to a "Packages.gz" or "Sources.gz" file */
static retvalue packagesdb_zprintout(packagesdb packagesdb,const char *filename) {
	retvalue ret;
	int r;
	gzFile pf;

	pf = gzopen(filename,"wb");
	if( !pf ) {
		fprintf(stderr,"Error creating '%s': %m\n",filename);
		/* if errno is zero, it's a memory error: */
		return RET_ERRNO(errno);
	}
	ret = packages_foreach(packagesdb,zprintout,pf,0);
	r = gzclose(pf);
	if( r < 0 )
		RET_ENDUPDATE(ret,RET_ZERRNO(r));
	/* Writing an empty file is also something done */
	if( ret == RET_NOTHING )
		return RET_OK;
	return ret;
}


static retvalue export_writepackages(packagesdb packagesdb,const char *filename,indexcompression compression) {

	if( verbose > 4 ) {
		fprintf(stderr,"  writing to '%s'...\n",filename);
	}

	(void)unlink(filename);
	switch( compression ) {
		case ic_uncompressed:
			return packagesdb_printout(packagesdb,filename);
		case ic_gzip:
			return packagesdb_zprintout(packagesdb,filename);
	}
	assert( compression == 0 && compression != 0 );
	return RET_ERROR;
}

static char *comprconcat(const char *str2,const char *str3,indexcompression compression) {

	switch( compression ) {
		case ic_uncompressed:
			return mprintf("%s/%s",str2,str3);
		case ic_gzip:
			return mprintf("%s/%s.gz",str2,str3);
	}
	assert( compression == 0 && compression != 0 );
	return NULL;
}


retvalue exportmode_init(/*@out@*/struct exportmode *mode,bool_t uncompressed,bool_t hasrelease,const char *indexfile,/*@null@*//*@only@*/char *options) {
	mode->compressions[ic_uncompressed] = uncompressed;
	mode->compressions[ic_gzip] = TRUE;
	mode->hasrelease = hasrelease;
	mode->hook = NULL;
	mode->filename = strdup(indexfile);
	if( mode->filename == NULL )
		return RET_ERROR_OOM;
	//TODO: parse options
	return RET_OK;
}

retvalue export_target(const char *dirofdist,const char *relativedir,packagesdb packages,const struct exportmode *exportmode,struct strlist *releasedfiles, bool_t onlymissing, int force) {
	indexcompression compression;
	retvalue result,r;

	result = RET_NOTHING;

	//TODO: rewrite to .new managment and releasedfile file

	for( compression = 0 ; compression <= ic_max ; compression++) {
		if( exportmode->compressions[compression] ) {
			char *relfilename,*reltmpfilename,*fullfilename;
			bool_t alreadyexists;

			relfilename = comprconcat(relativedir,
					exportmode->filename,compression);
			if( relfilename == NULL )
				return RET_ERROR_OOM;
			fullfilename = calc_dirconcat(dirofdist,relfilename);
			if( fullfilename == NULL ) {
				free(relfilename);
				return RET_ERROR_OOM;
			}

			alreadyexists = isregularfile(fullfilename);
			free(fullfilename);

			if( alreadyexists && onlymissing ) {
				r = strlist_add(releasedfiles,relfilename);
				if( RET_WAS_ERROR(r) )
					return r;
				continue;
			}
			reltmpfilename = calc_addsuffix(relfilename,"new");
			if( reltmpfilename == NULL ) {
				free(relfilename);
				return RET_ERROR_OOM;
			}
			fullfilename = calc_dirconcat(dirofdist,reltmpfilename);
			if( fullfilename == NULL ) {
				free(relfilename);
				free(reltmpfilename);
				return RET_ERROR_OOM;
			}
			
			r = export_writepackages(packages,
					fullfilename,compression);
			free(fullfilename);
			RET_UPDATE(result,r);
			if( !force && RET_WAS_ERROR(r) )
				return r;
			// TODO: call hooks here..
			free(relfilename);
			r = strlist_add(releasedfiles,reltmpfilename);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return result;
}

void exportmode_done(struct exportmode *mode) {
	assert( mode != NULL);
	free(mode->filename);
	free(mode->hook);
}

retvalue export_checksums(const char *dirofdist,FILE *f,struct strlist *releasedfiles, int force) {
	retvalue result,r;
	int i,e;
	size_t l;

	result = RET_NOTHING;

#define checkwritten if( e < 0 ) { \
		e = errno; \
		fprintf(stderr,"Error writing in Release.new: %d=%m!\n",e); \
		return RET_ERRNO(e); \
	}


	e = fprintf(f,"MD5Sum:\n");
	checkwritten;

	for( i = 0 ; i < releasedfiles->count ; i++ ) {
		const char *relname = releasedfiles->values[i];
		char *fullfilename,*md5sum;

		fullfilename = calc_dirconcat(dirofdist,relname);
		if( fullfilename == NULL )
			return RET_ERROR_OOM;

		r = md5sum_read(fullfilename,&md5sum);
		if( !RET_IS_OK(r) ) {
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Cannot find %s\n",fullfilename);
				r = RET_ERROR_MISSING;
			}
			free(fullfilename);
			if( force > 0 ) {
				RET_UPDATE(result,r);
				continue;
			} else
				return r;
		}
		free(fullfilename);
		e = fputc(' ',f);
		if( e == 0 ){
			free(md5sum);
			e = -1;
			checkwritten;
		}
		e = fputs(md5sum,f);
		free(md5sum);
		checkwritten;
		e = fputc(' ',f) - 1;
		checkwritten;
		l = strlen(relname);
		if( l > 4 && strcmp(relname+(l-4),".new") == 0 ) {
			size_t written;
			written = fwrite(relname,sizeof(char),l-4,f);
			if( written != l-4 ) {
				e = -1;
				checkwritten;
			}
		} else {
			fputs(relname,f);
			checkwritten;
		}
		e = fputc('\n',f) - 1;
		checkwritten;
	}
	return result;
#undef checkwritten
}

retvalue export_finalize(const char *dirofdist,struct strlist *releasedfiles, int force, bool_t issigned) {
	retvalue result,r;
	int i,e;
	bool_t somethingwasdone;
	char *tmpfullfilename,*finalfullfilename;

	result = RET_NOTHING;
	somethingwasdone = FALSE;

	/* after all is written and all is computed, move all the files
	 * at once: */
	for( i = 0 ; i < releasedfiles->count ; i++ ) {
		const char *relname = releasedfiles->values[i];
		size_t l;

		l = strlen(relname);
		if( l <= 4 || strcmp(relname+(l-4),".new") != 0 )
			continue;
		tmpfullfilename = calc_dirconcat(dirofdist,relname);
		if( tmpfullfilename == NULL )
			return RET_ERROR_OOM;
		finalfullfilename = calc_dirconcatn(dirofdist,relname,l-4);
		if( finalfullfilename == NULL ) {
			free(tmpfullfilename);
			return RET_ERROR_OOM;
		}
		e = rename(tmpfullfilename,finalfullfilename);
		if( e < 0 ) {
			e = errno;
			fprintf(stderr,"Error moving %s to %s: %d=%m!",tmpfullfilename,finalfullfilename,e);
			r = RET_ERRNO(e);
			RET_UPDATE(result,r);
			/* if we moved anything yet, do not stop with
			 * later errors, as it is too late already */
			if( force <= 0 && !somethingwasdone ) {
				free(tmpfullfilename);
				free(finalfullfilename);
				return r;
			}
		} else
			somethingwasdone = TRUE;
		free(tmpfullfilename);
		free(finalfullfilename);
	}
	if( issigned ) {
		tmpfullfilename = calc_dirconcat(dirofdist,"Release.gpg.new");
		if( tmpfullfilename == NULL )
			return RET_ERROR_OOM;
		finalfullfilename = calc_dirconcat(dirofdist,"Release.gpg");
		if( finalfullfilename == NULL ) {
			free(tmpfullfilename);
			return RET_ERROR_OOM;
		}
		e = rename(tmpfullfilename,finalfullfilename);
		if( e < 0 ) {
			e = errno;
			fprintf(stderr,"Error moving %s to %s: %d=%m!",tmpfullfilename,finalfullfilename,e);
			r = RET_ERRNO(e);
			RET_UPDATE(result,r);
		}
		free(tmpfullfilename);
		free(finalfullfilename);
	}

	tmpfullfilename = calc_dirconcat(dirofdist,"Release.new");
	if( tmpfullfilename == NULL )
		return RET_ERROR_OOM;
	finalfullfilename = calc_dirconcat(dirofdist,"Release");
	if( finalfullfilename == NULL ) {
		free(tmpfullfilename);
		return RET_ERROR_OOM;
	}
	e = rename(tmpfullfilename,finalfullfilename);
	if( e < 0 ) {
		e = errno;
		fprintf(stderr,"Error moving %s to %s: %d=%m!",tmpfullfilename,finalfullfilename,e);
		r = RET_ERRNO(e);
		RET_UPDATE(result,r);
	}
	free(tmpfullfilename);
	free(finalfullfilename);

	return result;

}
