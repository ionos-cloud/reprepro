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

#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "dirs.h"
#include "names.h"
#include "files.h"
#include "packages.h"
#include "reference.h"
#include "md5sum.h"
#include "chunks.h"
#include "binaries.h"
#include "sources.h"
#include "release.h"

#ifndef STD_BASE_DIR
#define STD_BASE_DIR "/var/spool/mirrorer"
#endif

/* global options */
char 	*incommingdir = STD_BASE_DIR "/incomming",
	*ppooldir = "pool",
	*pooldir = STD_BASE_DIR "/pool",
	*distdir = STD_BASE_DIR "/dists",
	*dbdir = STD_BASE_DIR "/db",
	*confdir = STD_BASE_DIR "/conf";
int 	local = 0;
int	verbose = 0;
int	force = 0;
int	nothingiserror = 0;

int printargs(int argc,char *argv[]) {
	int i;

	fprintf(stderr,"argc: %d\n",argc);
	for( i=0 ; i < argc ; i++ ) {
		fprintf(stderr,"%s\n",argv[i]);
	}
	return 0;
}

int addmd5sums(int argc,char *argv[]) {
	char buffer[2000],*c,*m;
	DB *files;

	files = files_initialize(dbdir);
	if( !files )
		return 1;
	while( fgets(buffer,1999,stdin) != NULL ) {
		c = index(buffer,'\n');
		if( ! c ) {
			fprintf(stderr,"Line too long\n");
			files_done(files);
			return 1;
		}
		*c = 0;
		m = index(buffer,' ');
		if( ! m ) {
			fprintf(stderr,"Malformed line\n");
			files_done(files);
			return 1;
		}
		*m = 0; m++;
		files_add(files,buffer,m);

	}
	files_done(files);
	return 0;
}


int removedependency(int argc,char *argv[]) {
	DB *refs;
	retvalue ret,r;

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer release <identifier>\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	ret = references_removedependency(refs,argv[1]);
	r = references_done(refs);
	RET_ENDUPDATE(ret,r);
	return EXIT_RET(ret);
}


int dumpreferences(int argc,char *argv[]) {
	DB *refs;
	retvalue result,r;

	if( argc != 1 ) {
		fprintf(stderr,"mirrorer dumpreferences\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	result = references_dump(refs);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

struct fileref { DB *files,*refs; };

retvalue checkifreferenced(void *data,const char *filekey,const char *md5andsize) {
	struct fileref *dist = data;
	retvalue r;

	r = references_isused(dist->refs,filekey);
	if( r == RET_NOTHING ) {
		printf("%s\n",filekey);
		return RET_OK;		
	} else if( RET_IS_OK(r) ) {
		return RET_NOTHING;
	} else
		return r;
}

int dumpunreferenced(int argc,char *argv[]) {
	retvalue result,r;
	struct fileref dist;

	if( argc != 1 ) {
		fprintf(stderr,"mirrorer dumpunreferenced\n");
		return 1;
	}
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs )
		return 1;
	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		references_done(dist.refs);
		return 1;
	}
	result = files_foreach(dist.files,checkifreferenced,&dist);
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

int addreference(int argc,char *argv[]) {
	DB *refs;
	retvalue result,r;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer addreference <reference> <referee>\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	result = references_adddependency(refs,argv[1],argv[2]);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}


int exportpackages(int argc,char *argv[]) {
	retvalue result;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer genpackages <identifier> <Packages-file to create>\n");
		return 1;
	}
	result = packages_doprintout(dbdir,argv[1],argv[2]);
	return EXIT_RET(result);
}


int zexportpackages(int argc,char *argv[]) {
	retvalue result;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer genzpackages <identifier> <Packages-file to create>\n");
		return 1;
	}
	result = packages_dozprintout(dbdir,argv[1],argv[2]);
	return EXIT_RET(result);
}

/****** common for reference{binaries,sources} *****/
struct referee {
	DB *refs;
	const char *identifier;
};

/**********************************referencebinaries**********************/

retvalue reference_binary(void *data,const char *package,const char *chunk) {
	struct referee *dist = data;
	const char *f;
	char *filekey;
	retvalue r;

	f = chunk_getfield("Filename",chunk);
	if( !f ) {
		fprintf(stderr,"No Filename-entry in package '%s'. Perhaps no binary entry?:\n%s\n",package,chunk);
		return RET_NOTHING;
	}
	filekey = chunk_dupvaluecut(f,ppooldir);
	if( !filekey ) {
		fprintf(stderr,"Error extracing filename from chunk: %s\n",chunk);
		return RET_ERROR;
	}
	if( verbose > 10 )
		fprintf(stderr,"referencing filekey: %s\n",filekey);
	r = references_adddependency(dist->refs,filekey,dist->identifier);
	free(filekey);
	return r;
}

int referencebinaries(int argc,char *argv[]) {
	retvalue result,r;
	struct referee dist;
	DB *pkgs;

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer referencebinaries <identifier>\n");
		return 1;
	}
	dist.identifier = argv[1];
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs )
		return 1;
	pkgs = packages_initialize(dbdir,argv[1]);
	if( ! pkgs ) {
		references_done(dist.refs);
		return 1;
	}
	result = packages_foreach(pkgs,reference_binary,&dist);

	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/**********************************referencesources**********************/


retvalue reference_source(void *data,const char *package,const char *chunk) {
	struct referee *dist = data;
	const char *f,*nextfile;
	char *dir,*files,*filekey,*filename;
	retvalue ret,r;

	f = chunk_getfield("Directory",chunk);
	if( !f ) {
		fprintf(stderr,"No Directory-entry in package '%s'. Perhaps no source entry?:\n%s\n",package,chunk);
		return RET_NOTHING;
	}
	dir = chunk_dupvaluecut(f,ppooldir);
	if( !dir ) {
		fprintf(stderr,"Error extracing directory from chunk: %s\n",chunk);
		return RET_ERROR;
	}
	f = chunk_getfield("Files",chunk);
	if( !f ) {
		free(dir);
		fprintf(stderr,"No Files-entry in package '%s'. Perhaps no source entry?:\n%s\n",package,chunk);
		return RET_NOTHING;
	}
	files = chunk_dupextralines(f);
	if( !files ) {
		free(dir);
		fprintf(stderr,"Error extracing files from chunk: %s\n",chunk);
		return RET_ERROR;
	}
	if( verbose > 10 )
		fprintf(stderr,"referencing source package: %s\n",package);
	nextfile = files;
	ret = RET_NOTHING;
	while( RET_IS_OK(r =sources_getfile(&nextfile,&filename,NULL)) ){
		filekey = calc_srcfilekey(dir,filename);
		if( !filekey) {
			free(dir);free(files);free(filename);
			return RET_ERROR;
		}
		r = references_adddependency(dist->refs,filekey,dist->identifier);
		free(filekey);free(filename);
		if( RET_WAS_ERROR(r) )
			break;
		RET_UPDATE(ret,r);
	}
	RET_UPDATE(ret,r);
	free(dir);free(files);
	return ret;
}

int referencesources(int argc,char *argv[]) {
	retvalue result,r;
	struct referee dist;
	DB *pkgs;

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer referencesources <identifier>\n");
		return 1;
	}
	dist.identifier = argv[1];
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs )
		return 1;
	pkgs = packages_initialize(dbdir,argv[1]);
	if( ! pkgs ) {
		references_done(dist.refs);
		return 1;
	}
	result = packages_foreach(pkgs,reference_source,&dist);

	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/****** common for [prepare]add{sources,packages} *****/

struct distribution {
	DB *files,*pkgs;
	const char *referee,*part;
};

/***********************************addsources***************************/

retvalue add_source(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,const char *oldchunk) {
	char *newchunk,*fulldir;
	retvalue ret,r;
	struct distribution *dist = (struct distribution*)data;
	const char *nextfile;
	char *filename,*filekey,*md5andsize;

	/* look for needed files */

	nextfile = files;
	ret = RET_NOTHING;
	while( RET_IS_OK(r=sources_getfile(&nextfile,&filename,&md5andsize)) ){
		filekey = calc_srcfilekey(directory,filename);
		
		r = files_expect(dist->files,pooldir,filekey,md5andsize);
		if( RET_WAS_ERROR(r) ) {
			free(filename);free(md5andsize);free(filekey);
			return r;
		}
		if( r == RET_NOTHING ) {
			/* File missing */
			printf("Missing file %s\n",filekey);
			free(filename);free(md5andsize);free(filekey);
			return RET_ERROR;
		}

		free(filename);free(md5andsize);free(filekey);
	}
	RET_UPDATE(ret,r);

	/* Add package to distribution's database */

	fulldir = calc_fullfilename(ppooldir,directory);
	if( !fulldir )
		return -1;

	newchunk = chunk_replaceentry(chunk,"Directory",fulldir);
	free(fulldir);
	if( !newchunk )
		return RET_ERROR;
	if( oldchunk != NULL ) {
		// TODO remove reference from old
		r = packages_replace(dist->pkgs,package,newchunk);
	} else {
		// TODO: add reference?
		r = packages_add(dist->pkgs,package,newchunk);
	}
	free(newchunk);
	RET_UPDATE(ret,r);
	return ret;
}

int addsources(int argc,char *argv[]) {
	int i;
	retvalue result,r;
	struct distribution dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer prepareaddsources <identifier> <part> <Sources-files>\n");
		return 1;
	}
	dist.referee = argv[1];
	dist.part = argv[2];

	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		return 1;
	}
	dist.pkgs = packages_initialize(dbdir,dist.referee);
	if( ! dist.pkgs ) {
		files_done(dist.files);
		return 1;
	}
	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = sources_add(dist.pkgs,dist.part,argv[i],add_source,&dist);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}
/****************************prepareaddsources********************************************/

retvalue showmissingsourcefiles(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,const char *oldchunk) {
	retvalue r,ret;
	struct distribution *dist = (struct distribution*)data;
	char *dn;
	const char *nextfile;
	char *filename,*filekey,*md5andsize;

	/* look for directory */
	if( (dn = calc_fullfilename(pooldir,directory))) {
		r = make_dir_recursive(dn);
		free(dn);
		if( r < 0 )
			return RET_ERROR;
	}

	nextfile = files;
	ret = RET_NOTHING;
	while( RET_IS_OK(r =sources_getfile(&nextfile,&filename,&md5andsize)) ){
		filekey = calc_srcfilekey(directory,filename);
		
		r = files_expect(dist->files,pooldir,filekey,md5andsize);
		if( RET_WAS_ERROR(r) ) {
			free(filename);free(md5andsize);free(filekey);
			return r;
		}
		if( r == RET_NOTHING ) {
			/* File missing */
			printf("%s/%s %s/%s\n",olddirectory,filename,pooldir,filekey);
			ret = RET_OK;
		}

		free(filename);free(md5andsize);free(filekey);
	}
	RET_UPDATE(ret,r);
	return ret;
}

int prepareaddsources(int argc,char *argv[]) {
	int i;
	retvalue r,result;
	struct distribution dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer prepareaddsources <identifier> <part> <Sources-files>\n");
		return 1;
	}
	dist.referee = argv[1];
	dist.part = argv[2];

	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		return 1;
	}
	dist.pkgs = packages_initialize(dbdir,dist.referee);
	if( ! dist.pkgs ) {
		files_done(dist.files);
		return 1;
	}
	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = sources_add(dist.pkgs,dist.part,argv[i],showmissingsourcefiles,&dist);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/****************************prepareaddpackages*******************************************/

retvalue showmissing(void *data,const char *chunk,const char *package,const char *sourcename,const char *oldfile,const char *filename,const char *filekey,const char *md5andsize,const char *oldchunk) {
	retvalue r;
	struct distribution *dist = (struct distribution*)data;
	char *fn;

	/* look for needed files */

	r = files_expect(dist->files,pooldir,filekey,md5andsize);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		/* look for directory */
		if( (fn = calc_fullfilename(pooldir,filekey))) {
			make_parent_dirs(fn);
			free(fn);
		}
		/* File missing */
		printf("%s %s/%s\n",oldfile,pooldir,filekey);
		return RET_OK;
	}
	return RET_NOTHING;
}

int prepareaddpackages(int argc,char *argv[]) {
	int i;
	retvalue r,result;
	struct distribution dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer prepareaddpackages <identifier> <part> <Packages-files>\n");
		return 1;
	}
	dist.referee = argv[1];
	dist.part = argv[2];

	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		return 1;
	}
	dist.pkgs = packages_initialize(dbdir,dist.referee);
	if( ! dist.pkgs ) {
		files_done(dist.files);
		return 1;
	}
	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = binaries_add(dist.pkgs,dist.part,argv[i],showmissing,&dist);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/***********************************addpackages*******************************************/

retvalue add_package(void *data,const char *chunk,const char *package,const char *sourcename,const char *oldfile,const char *filename,const char *filekey,const char *md5andsize,const char *oldchunk) {
	char *newchunk;
	char *filewithdir;
	retvalue r;
	struct distribution *dist = (struct distribution*)data;

	/* look for needed files */

	r = files_expect(dist->files,pooldir,filekey,md5andsize);
	if( ! RET_IS_OK(r) ) {
		printf("Missing file %s\n",filekey);
		return r;
	} 

	/* Add package to distribution's database */

	filewithdir = calc_fullfilename(ppooldir,filekey);
	if( !filewithdir )
		return RET_ERROR_OOM;
	newchunk = chunk_replaceentry(chunk,"Filename",filewithdir);
	free(filewithdir);
	if( !newchunk )
		return RET_ERROR;
	if( oldchunk != NULL ) {
		// TODO: remove old reference?
		r = packages_replace(dist->pkgs,package,newchunk);
	} else {
		// TODO: add reference?
		r = packages_add(dist->pkgs,package,newchunk);
	}
	free(newchunk);
	return r;
}


int addpackages(int argc,char *argv[]) {
	int i;
	retvalue r,result;
	struct distribution dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer addpackages <identifier> <part> <Packages-files>\n");
		return 1;
	}
	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		return 1;
	}
	dist.pkgs = packages_initialize(dbdir,argv[1]);
	if( ! dist.pkgs ) {
		files_done(dist.files);
		return 1;
	}
	dist.referee = argv[1];
	dist.part = argv[2];
	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = binaries_add(dist.pkgs,dist.part,argv[i],add_package,&dist);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}





int detect(int argc,char *argv[]) {
	DB *files;
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	ret = RET_NOTHING;
	files = files_initialize(dbdir);
	if( !files )
		return 1;
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			r = files_detect(files,pooldir,argv[i]);
			RET_UPDATE(ret,r);
		}

	} else
		while( fgets(buffer,4999,stdin) ) {
			nl = index(buffer,'\n');
			if( !nl ) {
				files_done(files);
				return 1;
			}
			*nl = '\0';
			r = files_detect(files,pooldir,buffer);
			RET_UPDATE(ret,r);
		} 
	r = files_done(files);
	RET_ENDUPDATE(ret,r);
	return EXIT_RET(ret);
}

int forget(int argc,char *argv[]) {
	DB *files;
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	files = files_initialize(dbdir);
	if( !files )
		return 1;
	ret = RET_NOTHING;
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			r = files_remove(files,argv[i]);
			RET_UPDATE(ret,r);
		}

	} else
		while( fgets(buffer,4999,stdin) ) {
			nl = index(buffer,'\n');
			if( !nl ) {
				files_done(files);
				return 1;
			}
			*nl = '\0';
			r = files_remove(files,buffer);
			RET_UPDATE(ret,r);
		} 
	r = files_done(files);
	RET_ENDUPDATE(ret,r);
	return EXIT_RET(ret);
}

int md5sums(int argc,char *argv[]) {
	DB *files;
	char *filename,*md;
	retvalue ret,r;
	int i;

	if( argc > 1 ) {
		ret = RET_NOTHING;
		for( i = 1 ; i < argc ; i++ ) {
			filename=calc_dirconcat(distdir,argv[i]);
			r = md5sum_and_size(&md,filename,0);
			RET_UPDATE(ret,r);
			if( RET_IS_OK(r) ) {
				printf(" %s %s\n",md,argv[i]);
				free(md);
				free(filename);
			} else {
				fprintf(stderr,"Error accessing file: %s: %m\n",filename);
				free(filename);
				if( ! force )
					return 1;
			}
		}
		return EXIT_RET(ret);
	} else {
		files = files_initialize(dbdir);
		if( !files )
			return 1;
		ret = files_printmd5sums(files);
		r = files_done(files);
		RET_ENDUPDATE(ret,r);
		return EXIT_RET(ret);
	}
}

int checkrelease(int argc,char *argv[]) {
	retvalue result;
	
	if( argc != 4 ) {
		fprintf(stderr,"mirrorer checkrelease <Release-file> <name to look up> <Packages or Sources-files to check>\n");
		return 1;
	}
	result = release_checkfile(argv[1],argv[2],argv[3]);
	return EXIT_RET(result);
}

struct data_binsrcexport { const struct release *release; const char *dirofdist;};

static retvalue exportbin(void *data,const char *component,const char *architecture) {
	retvalue result,r;
	struct data_binsrcexport *d = data;
	char *dbname,*filename;

	result = release_genbinary(d->release,architecture,component,distdir);
	dbname = mprintf("%s-%s-%s",d->release->codename,component,architecture);
	if( !dbname ) {
		return RET_ERROR_OOM;
	}
	filename = mprintf("%s/%s/binary-%s/Packages.gz",d->dirofdist,component,architecture);	
	if( !filename ) {
		free(dbname);
		return RET_ERROR_OOM;
	}
	if( verbose > 1 ) {
		fprintf(stderr,"Exporting %s...\n",dbname);
	}
	
	r = packages_dozprintout(dbdir,dbname,filename);
	RET_UPDATE(result,r);

	free(filename);
	filename = mprintf("%s/%s/binary-%s/Packages",d->dirofdist,component,architecture);	
	if( !filename ) {
		free(dbname);
		return RET_ERROR_OOM;
	}
	
	r = packages_doprintout(dbdir,dbname,filename);
	RET_UPDATE(result,r);

	free(filename);
	free(dbname);

	return result;
}

static retvalue exportsource(void *data,const char *component) {
	retvalue result,r;
	struct data_binsrcexport *d = data;
	char *dbname;
	char *filename;

	result = release_gensource(d->release,component,distdir);

	dbname = mprintf("%s-%s-src",d->release->codename,component);
	if( !dbname ) {
		return RET_ERROR_OOM;
	}
	filename = mprintf("%s/%s/source/Sources.gz",d->dirofdist,component);	
	if( !filename ) {
		free(dbname);
		return RET_ERROR_OOM;
	}
	if( verbose > 1 ) {
		fprintf(stderr,"Exporting %s...\n",dbname);
	}
	
	r = packages_dozprintout(dbdir,dbname,filename);
	RET_UPDATE(result,r);

	free(dbname);
	free(filename);

	return result;
}


static retvalue doexport(void *dummy,const struct release *release) {
	struct data_binsrcexport dat;
	retvalue result,r;
	char *dirofdist;

	if( verbose > 0 ) {
		fprintf(stderr,"Exporting %s...\n",release->codename);
	}
	dirofdist = calc_dirconcat(distdir,release->codename);
	if( !dirofdist ) {
		return RET_ERROR_OOM;
	}

	dat.release = release;
	dat.dirofdist = dirofdist;

	result = release_foreach_part(release,exportsource,exportbin,&dat);
	
	r = release_gen(release,distdir);
	RET_UPDATE(result,r);

	free(dirofdist);
	return result;
}

int export(int argc,char *argv[]) {
	retvalue result;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer export [<distributions>]\n");
		return 1;
	}
	
	result = release_foreach(confdir,argc-1,argv+1,doexport,NULL,force);
	return EXIT_RET(result);
}

/***********************rereferencing*************************/
struct data_binsrcreref { const struct release *release; DB *references;};

static retvalue rerefbin(void *data,const char *component,const char *architecture) {
	retvalue result,r;
	struct data_binsrcreref *d = data;
	char *dbname;
	struct referee refdata;
	DB *pkgs;

	dbname = mprintf("%s-%s-%s",d->release->codename,component,architecture);
	if( !dbname ) {
		return RET_ERROR_OOM;
	}
	if( verbose > 1 ) {
		if( verbose > 2 )
			fprintf(stderr,"Unlocking depencies of %s...\n",dbname);
		else
			fprintf(stderr,"Rereferencing %s...\n",dbname);
	}

	pkgs = packages_initialize(dbdir,dbname);
	if( ! pkgs ) {
		free(dbname);
		return RET_ERROR;
	}

	result = references_removedependency(d->references,dbname);

	if( verbose > 2 )
		fprintf(stderr,"Referencing %s...\n",dbname);

	refdata.refs = d->references;
	refdata.identifier = dbname;
	r = packages_foreach(pkgs,reference_binary,&refdata);
	RET_UPDATE(result,r);
	
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	free(dbname);

	return result;
}

static retvalue rerefsrc(void *data,const char *component) {
	retvalue result,r;
	struct data_binsrcreref *d = data;
	char *dbname;
	struct referee refdata;
	DB *pkgs;

	dbname = mprintf("%s-%s-src",d->release->codename,component);
	if( !dbname ) {
		return RET_ERROR_OOM;
	}
	if( verbose > 1 ) {
		if( verbose > 2 )
			fprintf(stderr,"Unlocking depencies of %s...\n",dbname);
		else
			fprintf(stderr,"Rereferencing %s...\n",dbname);
	}

	pkgs = packages_initialize(dbdir,dbname);
	if( ! pkgs ) {
		free(dbname);
		return RET_ERROR;
	}

	result = references_removedependency(d->references,dbname);

	if( verbose > 2 )
		fprintf(stderr,"Referencing %s...\n",dbname);

	refdata.refs = d->references;
	refdata.identifier = dbname;
	r = packages_foreach(pkgs,reference_source,&refdata);
	RET_UPDATE(result,r);
	
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	free(dbname);

	return result;
}


static retvalue rereference_dist(void *data,const struct release *release) {
	struct data_binsrcreref dat;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Referencing %s...\n",release->codename);
	}

	dat.release = release;
	dat.references = data;

	result = release_foreach_part(release,rerefsrc,rerefbin,&dat);
	
	return result;
}

int rereference(int argc,char *argv[]) {
	retvalue result,r;
	DB *refs;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer rereference [<distributions>]\n");
		return 1;
	}

	refs = references_initialize(dbdir);

	if( ! refs )
		return 1;
	
	result = release_foreach(confdir,argc-1,argv+1,rereference_dist,refs,force);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}

/*********************/
/* argument handling */
/*********************/

struct action {
	char *name;
	int (*start)(int argc,char *argv[]);
} actions[] = {
	{"d", printargs},
	{"detect", detect},
	{"forget", forget},
	{"md5sums", md5sums},
	{"checkrelease",checkrelease},
	{"prepareaddsources", prepareaddsources},
	{"addsources", addsources},
	{"prepareaddpackages", prepareaddpackages},
	{"addpackages", addpackages},
	{"genpackages", exportpackages},
	{"genzpackages", zexportpackages},
	{"export", export},
	{"rereference", rereference},
	{"addreference", addreference},
	{"printreferences", dumpreferences},
	{"printunreferenced", dumpunreferenced},
	{"dumpreferences", dumpreferences},
	{"dumpunreferenced", dumpunreferenced},
	{"removedependency", removedependency},
	{"referencebinaries",referencebinaries},
	{"referencesources",referencesources},
	{"addmd5sums",addmd5sums },
	{NULL,NULL}
};


int main(int argc,char *argv[]) {
	static struct option longopts[] = {
		{"local", 0, 0, 'l'},
		{"basedir", 1, 0, 'b'},
		{"incommingdir", 1, 0, 'i'},
		{"ppooldir", 1, 0, 'P'},
		{"pooldir", 1, 0, 'p'},
		{"distdir", 1, 0, 'd'},
		{"dbdir", 1, 0, 'D'},
		{"confdir", 1, 0, 'c'},
		{"help", 0, 0, 'h'},
		{"verbose", 0, 0, 'v'},
		{"nothingiserror", 0, 0, 'e'},
		{"force", 0, 0, 'f'},
		{0, 0, 0, 0}
	};
	int c;struct action *a;


	while( (c = getopt_long(argc,argv,"+feVvhlb:P:p:d:c:D:i:",longopts,NULL)) != -1 ) {
		switch( c ) {
			case 'h':
				printf(
"mirrorer - Manage a debian-mirror\n\n"
"options:\n"
" -h, --help:             Show this help\n"
" -l, --local:            Do only process the given file.\n"
"                         (i.e. do not look at .tar.gz when getting .dsc)\n"
" -b, --basedir <dir>:    Base-dir (will overwrite prior given -i, -p, -d, -D).\n"
" -i, --incomming <dir>:  incomming-Directory.\n"
" -p, --pooldir <dir>:    Directory to place the \"pool\" in.\n"
" -P, --ppooldir <dir>:   Prefix to place in generated Packages-files for pool.\n"
" -d, --distdir <dir>:    Directory to place the \"dists\" dir in.\n"
" -D, --dbdir <dir>:      Directory to place the database in.\n"
" -c, --confdir <dir>:    Directory to search configuration in.\n"
"\n"
"actions:\n"
" add <filename>:     Not yet implemented.\n"
" forget <file>:      Forget the given files (read stdin if none)\n"
"                     (Only usefull to unregister files manually deleted)\n"
" detect <file>:      Add given files to the database (read stdin if none)\n"
"  ('find $pooldir -type f -printf \"%%P\\n\" | mirrorer -p $pooldir inventory'\n"
"   will iventory an already existing pool-dir\n"
"   WARNING: names relative to pool-dir in shortest possible form\n"
" removedependency <type>:     Remove all marks \"Needed by <type>\"\n"
" referencebinaries <identifer>:\n"
"       Mark everything in dist <identifier> to be needed by <identifier>\n"
" referencesources <identifer>:\n"
"       Mark everything in dist <identifier> to be needed by <identifier>\n"
" printreferences:    Print all saved references\n"
" printunreferenced:  Print registered files withour reference\n"
" export              generate Packages.gz/Packages/Sources.gz/Release\n"
" addpackages <identifier> <part> <files>:\n"
"       Add the contents of Packages-files <files> to dist <identifier>\n"
" prepareaddpackages <identifier> <part> <files>:\n"
"       Search for missing files and print those not found\n"
" addsources <identifier> <part> <files>:\n"
"       Add the contents of Sources-files <files> to dist <identifier>\n"
" prepareaddsources <identifier> <part> <files>:\n"
"       Search for missing files and print those not found\n"
"\n"
						);
				exit(0);
				break;
			case 'l':
				local = 1;
				break;
			case 'v':
				verbose++;
				break;
			case 'V':
				verbose+=5;
				break;
			case 'e':
				nothingiserror=1;
				break;
			case 'f':
				force++;
				break;
			case 'b':
				incommingdir=mprintf("%s/incomming",optarg);
				pooldir=mprintf("%s/pool",optarg);
				distdir=mprintf("%s/dists",optarg);
				dbdir=mprintf("%s/db",optarg);
				confdir=mprintf("%s/conf",optarg);
				break;
			case 'i':
				incommingdir = strdup(optarg);
				break;
			case 'P':
				ppooldir = strdup(optarg);
				break;
			case 'p':
				pooldir = strdup(optarg);
				break;
			case 'd':
				distdir = strdup(optarg);
				break;
			case 'D':
				dbdir = strdup(optarg);
				break;
			case 'c':
				confdir = strdup(optarg);
				break;
			default:
				fprintf (stderr,"Not supported option '-%c'\n", c);
				exit(1);
		}
	}
	if( optind >= argc ) {
		fprintf(stderr,"No action given. (see --help for available options and actions)\n");
		exit(1);
	}

	a = actions;
	while( a->name ) {
		if( strcasecmp(a->name,argv[optind]) == 0 ) {
			return a->start(argc-optind,argv+optind);
		} else
			a++;
	}

	fprintf(stderr,"Unknown action '%s'. (see --help for available options and actions)\n",argv[optind]);
	return 2;
}

