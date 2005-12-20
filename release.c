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
#include "md5.h"
#include "md5sum.h"
#include "copyfile.h"
#include "dirs.h"
#include "names.h"
#include "signature.h"
#include "distribution.h"
#include "release.h"

extern int verbose;


struct release {
	/* The base-directory of the distribution we are exporting */
	char *dirofdist;
	/* anything new yet added */
	bool_t new;
	/* the files yet for the list */
	struct release_entry {
		struct release_entry *next;
		char *relativefilename;
		char *md5sum;
		/* both == NULL if not new, only final==NULL means delete */
		char *fullfinalfilename;
		char *fulltemporaryfilename;
	} *files;
	/* some more things, only here to make
	 * free'ing them in case of error easier */
	char *newreleasefilename,*releasefilename;
	char *newsignfilename,*signfilename;
};

void release_free(struct release *release) {
	struct release_entry *e;

	free(release->dirofdist);
	while( (e = release->files) != NULL ) {
		release->files = e->next;
		free(e->relativefilename);
		free(e->md5sum);
		free(e->fullfinalfilename);
		free(e->fulltemporaryfilename);
		free(e);
	}
	free(release->newreleasefilename);
	free(release->releasefilename);
	free(release->newsignfilename);
	free(release->signfilename);
	free(release);
}

const char *release_dirofdist(struct release *release) {
	return release->dirofdist;
}

static retvalue newreleaseentry(struct release *release, /*@only@*/ char *relativefilename, 
		/*@only@*/ char *md5sum,
		/*@only@*/ /*@null@*/ char *fullfinalfilename,
		/*@only@*/ /*@null@*/ char *fulltemporaryfilename ) {
	struct release_entry *n,*p;
	n = malloc(sizeof(struct release_entry));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->next = NULL;
	n->relativefilename = relativefilename;
	n->md5sum = md5sum;
	n->fullfinalfilename = fullfinalfilename;
	n->fulltemporaryfilename = fulltemporaryfilename;
	if( release->files == NULL )
		release->files = n;
	else {
		p = release->files;
		while( p->next != NULL )
			p = p->next;
		p->next = n;
	}
	return RET_OK;
}

retvalue release_init(const char *dbdir, const char *distdir, const char *codename, struct release **release) {
	struct release *n;
	n = calloc(1,sizeof(struct release));
	n->dirofdist = calc_dirconcat(distdir,codename);
	if( n->dirofdist == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}
	// TODO: open checksum cache database here
	*release = n;
	return RET_OK;
}

retvalue release_adddel(struct release *release,/*@only@*/char *reltmpfile) {
	char *filename;

	filename = calc_dirconcat(release->dirofdist,reltmpfile);
	if( filename == NULL ) {
		free(reltmpfile);
		return RET_ERROR_OOM;
	}
	free(reltmpfile);
	return newreleaseentry(release,NULL,NULL,NULL,filename);
}

retvalue release_addnew(struct release *release,/*@only@*/char *reltmpfile,/*@only@*/char *relfilename) {
	retvalue r;
	char *filename,*finalfilename,*md5sum;

	filename = calc_dirconcat(release->dirofdist,reltmpfile);
	if( filename == NULL ) {
		free(reltmpfile);
		free(relfilename);
		return RET_ERROR_OOM;
	}
	free(reltmpfile);
	r = md5sum_read(filename,&md5sum);
	if( !RET_IS_OK(r) ) {
		free(relfilename);
		free(filename);
		return r;
	}
	finalfilename = calc_dirconcat(release->dirofdist,relfilename);
	if( finalfilename == NULL ) {
		free(relfilename);
		free(filename);
		free(md5sum);
		return RET_ERROR_OOM;
	}
	release->new = TRUE;
	return newreleaseentry(release,relfilename,md5sum,finalfilename,filename);
}

retvalue release_addold(struct release *release,/*@only@*/char *relfilename) {
	retvalue r;
	char *filename,*md5sum;

	filename = calc_dirconcat(release->dirofdist,relfilename);
	if( filename == NULL ) {
		free(filename);
		return RET_ERROR_OOM;
	}
	r = md5sum_read(filename,&md5sum);
	if( !RET_IS_OK(r) ) {
		free(relfilename);
		free(filename);
		return r;
	}
	return newreleaseentry(release,relfilename,md5sum,filename,NULL);
}

static retvalue release_usecached(struct release *release,
				const char *relfilename,
				compressionset compressions) {
	retvalue r;
	char *filename;
	char *gzfilename;

	filename = calc_dirconcat(release->dirofdist,relfilename);
	if( filename == NULL ) {
		return RET_ERROR_OOM;
	}
	if( (compressions & IC_FLAG(ic_gzip)) != 0 ) {
		gzfilename = calc_addsuffix(filename,"gz");
		if( gzfilename == NULL ) {
			free(filename);
			return RET_ERROR_OOM;
		}
	} else
		gzfilename = NULL;

	/* todo: get md5sum from cache database here instead */
	if( isregularfile(filename) && 
			(gzfilename == NULL || isregularfile(gzfilename)) ) {
		char *md5sum,*gzmd5sum;
		r = md5sum_read(filename,&md5sum);
		if( RET_WAS_ERROR(r) ) {
			free(filename);
			free(gzfilename);
			return r;
		}
		assert( r != RET_NOTHING );
		free(filename);
		filename = strdup(relfilename);
		if( filename == NULL ) {
			free(md5sum);
			free(gzfilename);
			return RET_ERROR_OOM;
		}
		r = newreleaseentry(release,filename,md5sum,NULL,NULL);
		if( RET_WAS_ERROR(r) ) {
			free(gzfilename);
			return r;
		}
		if( gzfilename == NULL )
			return r;

		r = md5sum_read(gzfilename,&gzmd5sum);
		if( RET_WAS_ERROR(r) ) {
			free(gzfilename);
			return r;
		}
		assert( r != RET_NOTHING );
		free(gzfilename);
		gzfilename = calc_addsuffix(relfilename,"gz");
		if( gzfilename == NULL ) {
			free(gzmd5sum);
			return RET_ERROR_OOM;
		}
		r = newreleaseentry(release,gzfilename,gzmd5sum,NULL,NULL);

		return r;
	}
	free(filename);
	free(gzfilename);
	return RET_NOTHING;
}

struct filetorelease {
	char *relativefilename;
	/* those are NULL if not written to a file */
	char *fullfinalfilename;
	char *fulltemporaryfilename;
	/* all NULL if no compression set */
	char *relativegzfilename;
	char *fullfinalgzfilename;
	char *fulltemporarygzfilename;
	FILE *f;
	gzFile gzf;
	struct MD5Context context;off_t filesize;
};

void release_abortfile(struct filetorelease *file) {
	free(file->relativefilename);
	free(file->relativegzfilename);
	free(file->fullfinalfilename);
	free(file->fulltemporaryfilename);
	free(file->fullfinalgzfilename);
	free(file->fulltemporarygzfilename);
	if( file->f != NULL )
		(void)fclose(file->f);
	if( file->gzf != NULL )
		(void)gzclose(file->gzf);
}

bool_t release_oldexists(struct filetorelease *file) {
	if( file->fullfinalfilename != NULL ) {
		if( file->fullfinalgzfilename != NULL ) {
			return isregularfile(file->fullfinalgzfilename) &&
			       isregularfile(file->fullfinalfilename);
		} else {
			return isregularfile(file->fullfinalfilename);
		}
	} else {
		assert( file->fullfinalgzfilename != NULL );
		return isregularfile(file->fullfinalgzfilename);
	}
}

static retvalue startfile(struct release *release, 
		char *filename, compressionset compressions, 
		bool_t usecache,
		struct filetorelease **file) {
	struct filetorelease *n;

	if( usecache ) {
		retvalue r = release_usecached(release,filename,compressions);
		if( r != RET_NOTHING ) {
			free(filename);
			if( RET_IS_OK(r) )
				return RET_NOTHING;
			return r;
		}
	}

	n = calloc(1,sizeof(struct filetorelease));
	if( n == NULL ) {
		free(filename);
		return RET_ERROR_OOM;
	}
	n->relativefilename = filename;
	if( n->relativefilename == NULL ) {
		release_abortfile(n);
		return RET_ERROR_OOM;
	}
	if( (compressions & IC_FLAG(ic_uncompressed)) != 0 ) {
		n->fullfinalfilename = calc_dirconcat(release->dirofdist,n->relativefilename);
		if( n->fullfinalfilename == NULL ) {
			release_abortfile(n);
			return RET_ERROR_OOM;
		}
		n->fulltemporaryfilename = calc_addsuffix(n->fullfinalfilename,"new");
		if( n->fulltemporaryfilename == NULL ) {
			release_abortfile(n);
			return RET_ERROR_OOM;
		}
		(void)unlink(n->fulltemporaryfilename);
		n->f = fopen(n->fulltemporaryfilename,"wb");
		if( n->f == NULL ) {
			int e = errno;
			fprintf(stderr,"Error opening file %s for writing: %m\n",
					n->fulltemporaryfilename);
			release_abortfile(n);
			return RET_ERRNO(e);
		}
	} else
		n->f = NULL;
	if( (compressions & IC_FLAG(ic_gzip)) != 0 ) {
		n->relativegzfilename = calc_addsuffix(filename,"gz");
		if( n->relativegzfilename == NULL ) {
			release_abortfile(n);
			return RET_ERROR_OOM;
		}
		n->fullfinalgzfilename = calc_dirconcat(release->dirofdist,n->relativegzfilename);
		if( n->fullfinalgzfilename == NULL ) {
			release_abortfile(n);
			return RET_ERROR_OOM;
		}
		n->fulltemporarygzfilename = calc_addsuffix(n->fullfinalgzfilename,"new");
		if( n->fulltemporarygzfilename == NULL ) {
			release_abortfile(n);
			return RET_ERROR_OOM;
		}
		(void)unlink(n->fulltemporarygzfilename);
		n->gzf = gzopen(n->fulltemporarygzfilename,"wb");
		if( n->gzf == NULL ) {
			int e = errno;
			fprintf(stderr,"Error opening file %s for writing: %m\n",
					n->fulltemporarygzfilename);
			release_abortfile(n);
			return RET_ERRNO(e);
		}
	} else
		n->gzf = NULL;
	MD5Init(&n->context);
	*file = n;
	return RET_OK;
}

retvalue release_startfile2(struct release *release, 
		const char *relative_dir, const char *filename, compressionset compressions, 
		bool_t usecache,
		struct filetorelease **file) {
	char *relfilename;

	relfilename = calc_dirconcat(relative_dir,filename);
	if( relfilename == NULL )
		return RET_ERROR_OOM;
	return startfile(release,relfilename,compressions,usecache,file);
}
retvalue release_startfile(struct release *release, 
		const char *filename, compressionset compressions, 
		bool_t usecache,
		struct filetorelease **file) {
	char *relfilename;

	relfilename = strdup(filename);
	if( relfilename == NULL )
		return RET_ERROR_OOM;
	return startfile(release,relfilename,compressions,usecache,file);
}

retvalue release_finishfile(struct release *release, struct filetorelease *file) {
	retvalue result,r;
	char *md5sum;

	//TODO: use ferror

	if( file->f != NULL ) {
		if( fclose(file->f) != 0 ) {
			file->f = NULL;
			release_abortfile(file);
			return RET_ERRNO(errno);
		}
		file->f = NULL;
	}
	if( file->gzf != NULL ) {
		int ret;
		ret = gzclose(file->gzf);
		file->gzf = NULL;
		if( ret < 0 ) {
			release_abortfile(file);
			return RET_ZERRNO(ret);
		}
	}
	release->new = TRUE;
	result = RET_OK;
	assert((file->fullfinalfilename == NULL && file->fulltemporaryfilename == NULL)||
	       (file->fullfinalfilename != NULL && file->fulltemporaryfilename != NULL));

	r = md5sum_genstring(&md5sum,&file->context,file->filesize);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		release_abortfile(file);
		return r;
	}
	r = newreleaseentry(release,file->relativefilename,md5sum,
			file->fullfinalfilename, file->fulltemporaryfilename);
	RET_UPDATE(result,r);
	if( file->fullfinalgzfilename != NULL 
			&& file->fulltemporarygzfilename != NULL 
			&& file->relativegzfilename != NULL ) {
		/* todo: use the calculated values instead */
		r = md5sum_read(file->fulltemporarygzfilename,&md5sum);
		if( RET_WAS_ERROR(r) ) {
			release_abortfile(file);
			return r;
		}
		assert( r != RET_NOTHING );
		r = newreleaseentry(release,file->relativegzfilename,md5sum,
			file->fullfinalgzfilename, file->fulltemporarygzfilename);
		RET_UPDATE(result,r);
	} else {
		assert( file->fullfinalgzfilename == NULL && file->fulltemporarygzfilename == NULL && file->relativegzfilename == NULL);
	}
	free(file);
	return result;
}

static retvalue writeuncompressed(struct filetorelease *file, const char *data, size_t len) {
	size_t written;

	file->filesize += len;
	MD5Update(&file->context,data,len);

	if( file->f == NULL )
		return RET_NOTHING;

	while( len > 0 ) {
		written = fwrite(data,1,len,file->f);
		len -= written;
		data += written;
		if( ferror(file->f) )
			return RET_ERROR;
	}
	return RET_OK;
}
static retvalue writegz(struct filetorelease *file, const char *data, size_t len) {
	size_t written;

	if( file->gzf == NULL )
		return RET_NOTHING;

	while( len > 0 ) {
		written = gzwrite(file->gzf,data,len);
		if( written != len )
			return RET_ERROR;
		len -= written;
		data += written;
	}
	return RET_OK;
}
retvalue release_writedata(struct filetorelease *file, const char *data, size_t len) {
	retvalue result,r;
 
	result = RET_OK;
	r = writeuncompressed(file,data,len);
	RET_UPDATE(result,r);
	r = writegz(file,data,len);
	RET_UPDATE(result,r);
	return result;
}

/* Generate a "Release"-file for arbitrary directory */
retvalue release_directorydescription(struct release *release, const struct distribution *distribution,const struct target *target,const char *releasename,bool_t onlyifneeded) {
	retvalue r;
	struct filetorelease *f;

	r = release_startfile2(release,
		target->relativedirectory, releasename,
		IC_FLAG(ic_uncompressed), onlyifneeded,
		&f);
	if( RET_WAS_ERROR(r) || r == RET_NOTHING )
		return r;

#define release_writeheader(name, data) \
	        if( data != NULL ) { \
			(void)release_writestring(f,name ": "); \
			(void)release_writestring(f,data); \
			(void)release_writestring(f,"\n"); \
		}

	release_writeheader("Archive",distribution->suite);
	release_writeheader("Version",distribution->version);
	release_writeheader("Component",target->component);
	release_writeheader("Origin",distribution->origin);
	release_writeheader("Label",distribution->label);
	release_writeheader("Architecture",target->architecture);
	release_writeheader("Description",distribution->description);
	r = release_finishfile(release, f);
	return r;
}

/* Generate a main "Release" file for a distribution */
retvalue release_write(/*@only@*/struct release *release, struct distribution *distribution, bool_t onlyifneeded) {
	FILE *f;
	size_t s;
	int e;
	retvalue result,r;
	char buffer[100];
	time_t t;
	struct tm *gmt;
	int i;
	struct release_entry *file;
	bool_t somethingwasdone;

	if( onlyifneeded && !release->new ) {
		release_free(release);
		return RET_NOTHING;
	}

	(void)time(&t);
	gmt = gmtime(&t);
	if( gmt == NULL ) {
		release_free(release);
		return RET_ERROR_OOM;
	}
	s=strftime(buffer,99,"%a, %d %b %Y %H:%M:%S +0000",gmt);
	if( s == 0 || s >= 99) {
		fprintf(stderr,"strftime is doing strange things...\n");
		release_free(release);
		return RET_ERROR;
	}

	release->newreleasefilename = calc_dirconcat(release->dirofdist,"Release.new");
	if( release->newreleasefilename == NULL ) {
		release_free(release);
		return RET_ERROR_OOM;
	}
	release->releasefilename = calc_dirconcat(release->dirofdist,"Release");
	if( release->releasefilename == NULL ) {
		release_free(release);
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(release->newreleasefilename);
	(void)unlink(release->newreleasefilename);
	f = fopen(release->newreleasefilename,"w");
	if( f == NULL ) {
		e = errno;
		fprintf(stderr,"Error writing file %s: %m\n",release->newreleasefilename);
		release_free(release);
		return RET_ERRNO(e);
	}
#define checkwritten if( e < 0 ) { \
		e = errno; \
		fprintf(stderr,"Error writing to %s: %d=$m!\n",release->newreleasefilename,e); \
		release_free(release); \
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
		fprintf(stderr,"Error writing to %s!\n",release->newreleasefilename);
		release_free(release);
		(void)fclose(f);
		return r;
	}
	if( distribution->description != NULL ) {
		e = fputs("\nDescription: ",f);
		checkwritten;
		e = fputs(distribution->description,f);
		checkwritten;
	}

	e = fputs("\nMD5Sum:\n",f);
	checkwritten;

	for( file = release->files ; file != NULL ; file = file->next ) {
		if( file->md5sum != NULL && file->relativefilename != NULL ) {
			e = fputc(' ',f) - 1;
			checkwritten;
			e = fputs(file->md5sum,f);
			checkwritten;
			e = fputc(' ',f) - 1;
			checkwritten;
			e = fputs(file->relativefilename,f);
			checkwritten;
			e = fputc('\n',f) - 1;
			checkwritten;
		}
	}

#undef checkwritten
	if( ferror(f) != 0 ) { 
		e = errno; 
		fprintf(stderr,"Error writing to %s: %d=$m!\n",
				release->newreleasefilename,e);
		release_free(release);
		(void)fclose(f);
		return RET_ERRNO(e);
	}
	if( fclose(f) < 0 ) {
		e = errno;
		fprintf(stderr,"Error writing to %s: %d=$m!\n",
				release->newreleasefilename,e);
		release_free(release);
		return RET_ERRNO(e);
	}

	if( distribution->signwith != NULL ) { 

		release->signfilename = calc_dirconcat(release->dirofdist,"Release.gpg");
		if( release->signfilename == NULL ) {
			release_free(release);
			return RET_ERROR_OOM;
		}
		release->newsignfilename = calc_dirconcat(release->dirofdist,"Release.gpg.new");
		if( release->newsignfilename == NULL ) {
			release_free(release);
			return RET_ERROR_OOM;
		}

		r = signature_sign(distribution->signwith,
				release->newreleasefilename,
				release->newsignfilename);
		if( RET_WAS_ERROR(r) ) {
			release_free(release);
			return r;
		}
	}

	somethingwasdone = FALSE;
	result = RET_OK;

	for( file = release->files ; file != NULL ; file = file->next ) {
		if( file->relativefilename == NULL ) {
			assert(file->fulltemporaryfilename != NULL );
			e = unlink(file->fulltemporaryfilename);
			if( e < 0 ) {
				e = errno;
				fprintf(stderr,"Error deleting %s: %m. (Will be ignored)\n",file->fulltemporaryfilename);
			}
		} else if( file->fulltemporaryfilename != NULL ) {
			e = rename(file->fulltemporaryfilename,
					file->fullfinalfilename);
			if( e < 0 ) {
				e = errno;
				fprintf(stderr,"Error moving %s to %s: %d=%m!",file->fulltemporaryfilename,file->fullfinalfilename,e);
				r = RET_ERRNO(e);
				/* after something was done, do not stop
				 * but try to do as much as possible */
				if( !somethingwasdone ) {
					release_free(release);
					return r;
				}
				RET_UPDATE(result,r);
			} else
				somethingwasdone = TRUE;
		}
	}
	if( release->newsignfilename != NULL && release->signfilename != NULL ) {
		e = rename(release->newsignfilename,release->signfilename);
		if( e < 0 ) {
			e = errno;
			fprintf(stderr,"Error moving %s to %s: %d=%m!",
					release->newsignfilename,
					release->signfilename,e);
			r = RET_ERRNO(e);
			/* after something was done, do not stop
			 * but try to do as much as possible */
			if( !somethingwasdone ) {
				release_free(release);
				return r;
			}
			RET_UPDATE(result,r);
		}
	}
	e = rename(release->newreleasefilename,release->releasefilename);
	if( e < 0 ) {
		e = errno;
		fprintf(stderr,"Error moving %s to %s: %d=%m!",
				release->newreleasefilename,
				release->releasefilename,e);
		r = RET_ERRNO(e);
		RET_UPDATE(result,r);
	}

	/* free everything */
	release_free(release);
	return result;
}

