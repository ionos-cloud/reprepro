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
#include <unistd.h>
#include <stdlib.h>
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
#include "download.h"
#include "updates.h"
#include "signature.h"
#include "extractcontrol.h"
#include "checkindeb.h"


#ifndef STD_BASE_DIR
#define STD_BASE_DIR "/var/spool/mirrorer"
#endif

/* global options */
static 
char 	*incommingdir = STD_BASE_DIR "/incomming",
	*mirrordir = STD_BASE_DIR ,
	*distdir = STD_BASE_DIR "/dists",
	*dbdir = STD_BASE_DIR "/db",
	*listdir = STD_BASE_DIR "/lists",
	*confdir = STD_BASE_DIR "/conf";
static int	local = 0;
static int	force = 0;
static int	nothingiserror = 0;
int		verbose = 0;

static int printargs(int argc,char *argv[]) {
	int i;

	fprintf(stderr,"argc: %d\n",argc);
	for( i=0 ; i < argc ; i++ ) {
		fprintf(stderr,"%s\n",argv[i]);
	}
	return 0;
}
static int extract_control(int argc,char *argv[]) {
	retvalue result;
	char *control;

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer __extractcontrol <.deb-file>\n");
		return 1;
	}

	result = extractcontrol(&control,argv[1]);
	
	if( RET_IS_OK(result) ) 
		printf("%s\n",control);
	return EXIT_RET(result);
}


static int addmd5sums(int argc,char *argv[]) {
	char buffer[2000],*c,*m;
	DB *files;
	retvalue result,r;

	if( argc != 1 ) {
		fprintf(stderr,"mirrorer _addmd5sums < <data>\n");
		return 1;
	}

	files = files_initialize(dbdir);
	if( !files )
		return 1;

	result = RET_NOTHING;
	
	while( fgets(buffer,1999,stdin) != NULL ) {
		c = index(buffer,'\n');
		if( ! c ) {
			fprintf(stderr,"Line too long\n");
			(void)files_done(files);
			return 1;
		}
		*c = '\0';
		m = index(buffer,' ');
		if( ! m ) {
			fprintf(stderr,"Malformed line\n");
			(void)files_done(files);
			return 1;
		}
		*m = '\0'; m++;
		r = files_add(files,buffer,m);
		RET_UPDATE(result,r);

	}
	r = files_done(files);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}


static int removereferences(int argc,char *argv[]) {
	DB *refs;
	retvalue ret,r;

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer _removereferences <identifier>\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	ret = references_remove(refs,argv[1]);
	r = references_done(refs);
	RET_ENDUPDATE(ret,r);
	return EXIT_RET(ret);
}


static int dumpreferences(int argc,char *argv[]) {
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

static retvalue checkifreferenced(void *data,const char *filekey,const char *md5andsize) {
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

static int dumpunreferenced(int argc,char *argv[]) {
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
		(void)references_done(dist.refs);
		return 1;
	}
	result = files_foreach(dist.files,checkifreferenced,&dist);
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

static retvalue deleteifunreferenced(void *data,const char *filekey,const char *md5andsize) {
	struct fileref *dist = data;
	retvalue r;
	char *filename;
	int err;

	r = references_isused(dist->refs,filekey);
	if( r == RET_NOTHING ) {
		if( verbose >= 0 )
			printf("deleting and forgetting %s\n\n",filekey);
		filename = calc_fullfilename(mirrordir,filekey);
		if( !filename )
			r = RET_ERROR_OOM;
		else {
			err = unlink(filename);
			if( err != 0 ) {
				r = RET_ERRNO(errno);
				fprintf(stderr,"error while unlinking %s: %m\n",filename);
			} else 
				r = files_remove(dist->files,filekey);
			free(filename);
		}
		return r;
	} else if( RET_IS_OK(r) ) {
		return RET_NOTHING;
	} else
		return r;
}

static int deleteunreferenced(int argc,char *argv[]) {
	retvalue result,r;
	struct fileref dist;

	if( argc != 1 ) {
		fprintf(stderr,"mirrorer deleteunreferenced\n");
		return 1;
	}
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs )
		return 1;
	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		(void)references_done(dist.refs);
		return 1;
	}
	result = files_foreach(dist.files,deleteifunreferenced,&dist);
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

static int addreference(int argc,char *argv[]) {
	DB *refs;
	retvalue result,r;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer _addreference <reference> <referee>\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	result = references_increment(refs,argv[1],argv[2]);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}


static int exportpackages(int argc,char *argv[]) {
	retvalue result;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer _genpackages <identifier> <Packages-file to create>\n");
		return 1;
	}
	result = packages_doprintout(dbdir,argv[1],argv[2]);
	return EXIT_RET(result);
}


static int zexportpackages(int argc,char *argv[]) {
	retvalue result;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer _genzpackages <identifier> <Packages-file to create>\n");
		return 1;
	}
	result = packages_dozprintout(dbdir,argv[1],argv[2]);
	return EXIT_RET(result);
}

static int removepackage(int argc,char *argv[]) {
	retvalue result,r;
	DB *pkgs,*refs;
	int i;
	char *filekey,*chunk;

	if( argc < 3 ) {
		fprintf(stderr,"mirrorer _removepackage <identifier> <package-name>\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	pkgs = packages_initialize(dbdir,argv[1]);
	if( ! pkgs ) {
		(void)references_done(refs);
		return 1;
	}
	
	result = RET_NOTHING;
	for( i = 2 ; i< argc ; i++ ) {
		chunk = packages_get(pkgs,argv[i]);
		if( verbose > 0 )
			fprintf(stderr,"removing '%s' from '%s'...\n",argv[i],argv[1]);
		r = packages_remove(pkgs,argv[i]);
		if( RET_IS_OK(r) ) {
			r = binaries_parse_chunk(chunk,NULL,&filekey,NULL,NULL,NULL);
			if( RET_IS_OK(r) ) {
				if( verbose > 1 )
					fprintf(stderr,"unreferencing '%s' to '%s' \n",argv[1],filekey);
				r = references_decrement(refs,filekey,argv[1]);
				free(filekey);
			} else if( r == RET_NOTHING ) {
				if( verbose > 1 )
					fprintf(stderr,"unreferencing needed srcfiles in '%s' about  '%s' \n",argv[1],argv[i]);
				r = sources_dereference(refs,argv[1],chunk);
			}
		}
		if( chunk )
			free(chunk);
		RET_UPDATE(result,r);
	}

	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}
/****** reference_{binary,source} *****/
struct referee {
	DB *refs;
	const char *identifier;
};

static retvalue reference_binary(void *data,const char *package,const char *chunk) {
	struct referee *dist = data;
	char *filekey;
	retvalue r;

	r = binaries_parse_chunk(chunk,NULL,&filekey,NULL,NULL,NULL);
	if( verbose >= 0 && r == RET_NOTHING ) {
		fprintf(stderr,"Package does not look binary: '%s'\n",chunk);
	}
	if( !RET_IS_OK(r) )
		return r;
	if( verbose > 10 )
		fprintf(stderr,"referencing filekey: %s\n",filekey);
	r = references_increment(dist->refs,filekey,dist->identifier);
	free(filekey);
	return r;
}

static retvalue reference_source(void *data,const char *package,const char *chunk) {
	struct referee *dist = data;
	char *dir,*version;
	struct strlist files;
	retvalue r;

	r = sources_parse_chunk(chunk,NULL,&version,&dir,&files);
	if( verbose >= 0 && r == RET_NOTHING ) {
		fprintf(stderr,"Package does not look like source: '%s'\n",chunk);
	}
	if( !RET_IS_OK(r) )
		return r;

	if( verbose > 10 )
		fprintf(stderr,"referencing source package: %s\n",package);


	r = sources_reference(dist->refs,dist->identifier,package,version,dir,&files);
	free(version); free(dir);strlist_done(&files);
	return r;
}


/****** common for [prepare]add{sources,packages} *****/

struct distributionhandles {
	DB *files,*pkgs,*refs;
	const char *referee,*component;
};

/***********************************addsources***************************/

static retvalue add_source(void *data,const char *chunk,const char *package,const char *version,const char *directory,const char *olddirectory,const struct strlist *files,const char *oldchunk) {
	char *newchunk;
	retvalue result,r;
	struct distributionhandles *dist = (struct distributionhandles*)data;
	int i;
	char *basefilename,*filekey,*md5andsize;

	/* look for needed files */


	for( i = 0 ; i < files->count ; i++ ) {
		r = sources_getfile(files->values[i],&basefilename,&md5andsize);
		if( RET_WAS_ERROR(r) )
			return r;
		filekey = calc_dirconcat(directory,basefilename);
		if( !filekey ) {
			free(basefilename);free(md5andsize);
			return RET_ERROR_OOM;
		}
		r = files_expect(dist->files,mirrordir,filekey,md5andsize);
		if( RET_WAS_ERROR(r) ) {
			free(basefilename);free(md5andsize);free(filekey);
			return r;
		}
		if( r == RET_NOTHING ) {
			/* File missing */
			printf("Missing file %s\n",filekey);
			free(basefilename);free(md5andsize);free(filekey);
			return RET_ERROR;
		}

		free(basefilename);free(md5andsize);free(filekey);
	}

	/* after all is there, reference it */
	r = sources_reference(dist->refs,dist->referee,package,version,directory,files);
	if( RET_WAS_ERROR(r) )
		return r;

	/* Add package to distribution's database */

	newchunk = chunk_replacefield(chunk,"Directory",directory);
	if( !newchunk )
		return RET_ERROR_OOM;
	if( oldchunk != NULL ) {
		result = packages_replace(dist->pkgs,package,newchunk);
	} else {
		result = packages_add(dist->pkgs,package,newchunk);
	}
	free(newchunk);

	if( RET_WAS_ERROR(result) )
		return result;

	/* remove no longer needed references */

	if( oldchunk != NULL ) {
		r = sources_dereference(dist->refs,dist->referee,oldchunk);
		RET_UPDATE(result,r);
	}
	return result;
}

static int addsources(int argc,char *argv[]) {
	int i;
	retvalue result,r;
	struct distributionhandles dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer addsources <identifier> <component> <Sources-files>\n");
		return 1;
	}

	dist.referee = argv[1];
	dist.component = argv[2];
	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		return 1;
	}
	dist.pkgs = packages_initialize(dbdir,dist.referee);
	if( ! dist.pkgs ) {
		(void)files_done(dist.files);
		return 1;
	}
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs ) {
		(void)files_done(dist.files);
		(void)packages_done(dist.pkgs);
		return 1;
	}
	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = sources_add(dist.pkgs,dist.component,argv[i],add_source,&dist,force);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}
/****************************prepareaddsources********************************************/

static retvalue showmissingsourcefiles(void *data,const char *chunk,const char *package,const char *version,const char *directory,const char *olddirectory,const struct strlist *files,const char *oldchunk) {
	retvalue r,ret;
	struct distributionhandles *dist = (struct distributionhandles*)data;
	char *dn;
	char *basefilename,*filekey,*md5andsize;
	int i;

	/* look for directory */
	if( (dn = calc_fullfilename(mirrordir,directory))) {
		r = dirs_make_recursive(dn);
		free(dn);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	ret = r = RET_NOTHING;
	for( i = 0 ; i < files->count ; i++ ) {
		r = sources_getfile(files->values[i],&basefilename,&md5andsize);
		if( RET_WAS_ERROR(r) )
			break;
		filekey = calc_srcfilekey(directory,basefilename);
		
		r = files_expect(dist->files,mirrordir,filekey,md5andsize);
		if( RET_WAS_ERROR(r) ) {
			free(basefilename);free(md5andsize);free(filekey);
			return r;
		}
		if( r == RET_NOTHING ) {
			/* File missing */
			printf("%s/%s %s/%s\n",olddirectory,basefilename,mirrordir,filekey);
			ret = RET_OK;
		}

		free(basefilename);free(md5andsize);free(filekey);
	}
	RET_UPDATE(ret,r);
	return ret;
}

static int prepareaddsources(int argc,char *argv[]) {
	int i;
	retvalue r,result;
	struct distributionhandles dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer prepareaddsources <identifier> <component> <Sources-files>\n");
		return 1;
	}

	dist.referee = argv[1];
	dist.component = argv[2];
	dist.refs = NULL;

	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		return 1;
	}
	dist.pkgs = packages_initialize(dbdir,dist.referee);
	if( ! dist.pkgs ) {
		(void)files_done(dist.files);
		return 1;
	}

	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = sources_add(dist.pkgs,dist.component,argv[i],showmissingsourcefiles,&dist,force);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/****************************prepareaddpackages*******************************************/

static retvalue showmissing(void *data,const char *chunk,const char *package,const char *sourcename,const char *oldfile,const char *basename,const char *filekey,const char *md5andsize,const char *oldchunk) {
	retvalue r;
	struct distributionhandles *dist = (struct distributionhandles*)data;
	char *fn;

	/* look for needed files */

	r = files_expect(dist->files,mirrordir,filekey,md5andsize);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_NOTHING ) {
		/* look for directory */
		if( (fn = calc_fullfilename(mirrordir,filekey))) {
			r = dirs_make_parent(fn);
			free(fn);
			if( RET_WAS_ERROR(r) )
				return r;
		} else
			return RET_ERROR_OOM;
		/* File missing */
		printf("%s %s/%s\n",oldfile,mirrordir,filekey);
		return RET_OK;
	}
	return RET_NOTHING;
}

static int prepareaddpackages(int argc,char *argv[]) {
	int i;
	retvalue r,result;
	struct distributionhandles dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer prepareaddpackages <identifier> <component> <Packages-files>\n");
		return 1;
	}

	dist.referee = argv[1];
	dist.component = argv[2];

	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		return 1;
	}
	dist.pkgs = packages_initialize(dbdir,dist.referee);
	if( ! dist.pkgs ) {
		(void)files_done(dist.files);
		return 1;
	}

	dist.refs = NULL;

	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = binaries_add(dist.pkgs,dist.component,argv[i],showmissing,&dist,force);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/***********************************addpackages*******************************************/

retvalue add_package(void *data,const char *chunk,const char *package,const char *sourcename,const char *oldfile,const char *basename,const char *filekey,const char *md5andsize,const char *oldchunk) {
	char *newchunk;
	retvalue result,r;
	char *oldfilekey;
	struct distributionhandles *d = data;

	/* look for needed files */

	r = files_expect(d->files,mirrordir,filekey,md5andsize);
	if( ! RET_IS_OK(r) ) {
		printf("Missing file %s\n",filekey);
		return r;
	} 

	/* Calculate new chunk, file to replace and checking */
	
	newchunk = chunk_replacefield(chunk,"Filename",filekey);
	if( !newchunk )
		return RET_ERROR;
	/* remove old references to files */

	if( oldchunk ) {
		r = binaries_parse_chunk(oldchunk,NULL,&oldfilekey,NULL,NULL,NULL);
		if( RET_WAS_ERROR(r) ) {
			free(newchunk);
			return r;
		}
	} else 
		oldfilekey = NULL;

	result = checkindeb_insert(d->refs,d->referee,d->pkgs,package,newchunk,filekey,oldfilekey);

	free(newchunk);
	free(oldfilekey);
	return result;
}


static int addpackages(int argc,char *argv[]) {
	int i;
	retvalue r,result;
	struct distributionhandles dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer addpackages <identifier> <component> <Packages-files>\n");
		return 1;
	}
	dist.files = files_initialize(dbdir);
	if( ! dist.files ) {
		return 1;
	}
	dist.pkgs = packages_initialize(dbdir,argv[1]);
	if( ! dist.pkgs ) {
		(void)files_done(dist.files);
		return 1;
	}
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs ) {
		(void)files_done(dist.files);
		(void)packages_done(dist.pkgs);
		return 1;
	}
	dist.referee = argv[1];
	dist.component = argv[2];
	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = binaries_add(dist.pkgs,dist.component,argv[i],add_package,&dist,force);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

static int detect(int argc,char *argv[]) {
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
			r = files_detect(files,mirrordir,argv[i]);
			RET_UPDATE(ret,r);
		}

	} else
		while( fgets(buffer,4999,stdin) ) {
			nl = index(buffer,'\n');
			if( !nl ) {
				(void)files_done(files);
				return 1;
			}
			*nl = '\0';
			r = files_detect(files,mirrordir,buffer);
			RET_UPDATE(ret,r);
		} 
	r = files_done(files);
	RET_ENDUPDATE(ret,r);
	return EXIT_RET(ret);
}

static int forget(int argc,char *argv[]) {
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
				(void)files_done(files);
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

static int md5sums(int argc,char *argv[]) {
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

static int checkrelease(int argc,char *argv[]) {
	retvalue result;
	
	if( argc != 4 ) {
		fprintf(stderr,"mirrorer checkrelease <Release-file> <name to look up> <Packages or Sources-files to check>\n");
		return 1;
	}
	result = release_checkfile(argv[1],argv[2],argv[3]);
	return EXIT_RET(result);
}

struct data_binsrcexport { const struct distribution *distribution; const char *dirofdist;int force;};

static retvalue exportbin(void *data,const char *component,const char *architecture) {
	retvalue result,r;
	struct data_binsrcexport *d = data;
	char *dbname,*filename;

	result = release_genbinary(d->distribution,architecture,component,distdir);
	dbname = mprintf("%s-%s-%s",d->distribution->codename,component,architecture);
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

	result = release_gensource(d->distribution,component,distdir);

	dbname = mprintf("%s-%s-src",d->distribution->codename,component);
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


static retvalue doexport(void *dummy,const char *chunk,const struct distribution *distribution) {
	struct data_binsrcexport dat;
	retvalue result,r;
	char *dirofdist;

	if( verbose > 0 ) {
		fprintf(stderr,"Exporting %s...\n",distribution->codename);
	}
	dirofdist = calc_dirconcat(distdir,distribution->codename);
	if( !dirofdist ) {
		return RET_ERROR_OOM;
	}

	dat.distribution = distribution;
	dat.dirofdist = dirofdist;

	result = distribution_foreach_part(distribution,exportsource,exportbin,&dat,force);
	
	r = release_gen(distribution,distdir,chunk);
	RET_UPDATE(result,r);

	free(dirofdist);
	return result;
}

static int export(int argc,char *argv[]) {
	retvalue result;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer export [<distributions>]\n");
		return 1;
	}
	
	result = distribution_foreach(confdir,argc-1,argv+1,doexport,NULL,force);
	return EXIT_RET(result);
}

/***********************update********************************/

static retvalue fetchupstreamlists(void *data,const char *chunk,const struct distribution *distribution,struct update *update) {
	retvalue result,r;
	char *from,*method;
	struct strlist todownload;

	/* * Get the data for the download backend * */
	r = chunk_getvalue(chunk,"From",&from);
	if( !RET_IS_OK(r) )
		// TODO: make NOTHING to error?
		return r;
	r = chunk_getvalue(chunk,"Method",&method);
	if( !RET_IS_OK(r) ) {
		// TODO: make NOTHING to error?
		free(from);
		return r;
	}

	/* * Calculate what to download * */

	r = strlist_init(&todownload);
	if( RET_WAS_ERROR(r) ) {
		free(from);free(method);
		return r;
	}

	r = updates_calcliststofetch(&todownload,
			listdir,distribution->codename,update->name,chunk,
			update->suite_from,
			&update->components_from,
			&update->architectures);

	if( RET_WAS_ERROR(r) ) {
		strlist_done(&todownload);
		free(from);free(method);
		return r;
	}

	/* * download * */

	result = download_fetchfiles(method,from,&todownload);
	strlist_done(&todownload);
	free(method);free(from);
	
	if( RET_WAS_ERROR(result) )
		return result;

	/* check the given .gpg of Release and the md5sums therein*/
	r = updates_checkfetchedlists(update,chunk,listdir,distribution->codename);

	RET_ENDUPDATE(result,r);
	
	return result;
}

static int update(int argc,char *argv[]) {
	retvalue result;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer update [<distributions>]\n");
		return 1;
	}

	result = dirs_make_recursive(listdir);	
	if( RET_WAS_ERROR(result) ) {
		return EXIT_RET(result);
	}
	result = updates_foreach(confdir,argc-1,argv+1,fetchupstreamlists,NULL,force);
	return EXIT_RET(result);
}

/***********************rereferencing*************************/
struct data_binsrcreref { const struct distribution *distribution; DB *references;};

static retvalue rerefbin(void *data,const char *component,const char *architecture) {
	retvalue result,r;
	struct data_binsrcreref *d = data;
	char *dbname;
	struct referee refdata;
	DB *pkgs;

	dbname = mprintf("%s-%s-%s",d->distribution->codename,component,architecture);
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

	result = references_remove(d->references,dbname);

	if( verbose > 2 )
		fprintf(stderr,"Referencing %s...\n",dbname);

	refdata.refs = d->references;
	refdata.identifier = dbname;
	r = packages_foreach(pkgs,reference_binary,&refdata,force);
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

	dbname = mprintf("%s-%s-src",d->distribution->codename,component);
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

	result = references_remove(d->references,dbname);

	if( verbose > 2 )
		fprintf(stderr,"Referencing %s...\n",dbname);

	refdata.refs = d->references;
	refdata.identifier = dbname;
	r = packages_foreach(pkgs,reference_source,&refdata,force);
	RET_UPDATE(result,r);
	
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	free(dbname);

	return result;
}


static retvalue rereference_dist(void *data,const char *chunk,const struct distribution *distribution) {
	struct data_binsrcreref dat;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Referencing %s...\n",distribution->codename);
	}

	dat.distribution = distribution;
	dat.references = data;

	result = distribution_foreach_part(distribution,rerefsrc,rerefbin,&dat,force);

	return result;
}

static int rereference(int argc,char *argv[]) {
	retvalue result,r;
	DB *refs;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer rereference [<distributions>]\n");
		return 1;
	}

	refs = references_initialize(dbdir);

	if( ! refs )
		return 1;
	
	result = distribution_foreach(confdir,argc-1,argv+1,rereference_dist,refs,force);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}
/***********************checking*************************/
struct data_binsrccheck { const struct distribution *distribution; DB *references; DB *files; const char *identifier;};


static retvalue check_binary(void *data,const char *package,const char *chunk) {
	struct data_binsrccheck *d = data;
	char *filekey;
	retvalue r;

	r = binaries_parse_chunk(chunk,NULL,&filekey,NULL,NULL,NULL);
	if( verbose >= 0 && r == RET_NOTHING ) {
		fprintf(stderr,"Package does not look binary: '%s'\n",chunk);
	}
	if( !RET_IS_OK(r) )
		return r;
	if( verbose > 10 )
		fprintf(stderr,"checking for filekey: %s\n",filekey);
	r = references_check(d->references,filekey,d->identifier);
	free(filekey);
	return r;
}

static retvalue checkbin(void *data,const char *component,const char *architecture) {
	retvalue result,r;
	struct data_binsrccheck *d = data;
	char *dbname;
	DB *pkgs;

	dbname = mprintf("%s-%s-%s",d->distribution->codename,component,architecture);
	if( !dbname ) {
		return RET_ERROR_OOM;
	}
	if( verbose > 1 ) {
		fprintf(stderr,"Checking %s...\n",dbname);
	}

	pkgs = packages_initialize(dbdir,dbname);
	if( ! pkgs ) {
		free(dbname);
		return RET_ERROR;
	}

	d->identifier = dbname;
	result = packages_foreach(pkgs,check_binary,d,force);
	
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	free(dbname);

	return result;
}

static retvalue check_source(void *data,const char *package,const char *chunk) {
	struct data_binsrccheck *d = data;
	char *dir,*filekey,*basefilename,*md5andsize;
	char *version,*identifier;
	struct strlist files;
	retvalue ret,r;
	int i;

	r = sources_parse_chunk(chunk,NULL,&version,&dir,&files);
	if( verbose >= 0 && r == RET_NOTHING ) {
		fprintf(stderr,"Package does not look like source: '%s'\n",chunk);
	}
	if( !RET_IS_OK(r) )
		return r;

	identifier = mprintf("%s %s %s",d->identifier,package,version);
	free(version);
	if( !identifier ) {
		free(dir);strlist_done(&files);
		return RET_ERROR_OOM;
	}

	if( verbose > 10 )
		fprintf(stderr,"referencing source package: %s\n",package);
	ret = RET_NOTHING;
	for( i = 0 ; i < files.count ; i++ ) {
		r = sources_getfile(files.values[i],&basefilename,&md5andsize);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(ret,r);
			break;
		}
		filekey = calc_srcfilekey(dir,basefilename);
		if( !filekey) {
			free(identifier);
			free(dir);strlist_done(&files);free(basefilename);
			return RET_ERROR;
		}
		r = references_check(d->references,filekey,identifier);
		RET_UPDATE(ret,r);

		r = files_check(d->files,filekey,md5andsize);
		if( r == RET_NOTHING) {
			fprintf(stderr,"Expected file '%s' not found in database!!\n",filekey);
			r = RET_ERROR;
		} else if( RET_WAS_ERROR(r) ) {
			fprintf(stderr,"Error looking for file '%s' with '%s' in database!!\n",filekey,md5andsize);
		}
		RET_UPDATE(ret,r);

		free(filekey);free(basefilename);free(md5andsize);
		if( RET_WAS_ERROR(ret) )
			break;
	}
	free(identifier);
	free(dir);strlist_done(&files);
	return ret;
}

static retvalue checksrc(void *data,const char *component) {
	retvalue result,r;
	struct data_binsrccheck *d = data;
	char *dbname;
	DB *pkgs;

	dbname = mprintf("%s-%s-src",d->distribution->codename,component);
	if( !dbname ) {
		return RET_ERROR_OOM;
	}
	if( verbose > 1 ) {
		fprintf(stderr,"Checking depencies of %s...\n",dbname);
	}

	pkgs = packages_initialize(dbdir,dbname);
	if( ! pkgs ) {
		free(dbname);
		return RET_ERROR;
	}

	d->identifier = dbname;
	result = packages_foreach(pkgs,check_source,d,force);
	
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);

	free(dbname);

	return result;
}


static retvalue check_dist(void *data,const char *chunk,const struct distribution *distribution) {
	struct data_binsrccheck *dat=data;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Checking %s...\n",distribution->codename);
	}


	dat->distribution = distribution;

	result = distribution_foreach_part(distribution,checksrc,checkbin,dat,force);
	
	return result;
}

static int check(int argc,char *argv[]) {
	retvalue result,r;
	struct data_binsrccheck dat;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer check [<distributions>]\n");
		return 1;
	}

	dat.references = references_initialize(dbdir);

	if( ! dat.references )
		return 1;

	dat.files = files_initialize(dbdir);

	if( ! dat.files ) {
		(void)references_done(dat.references);
		return 1;
	}
	
	result = distribution_foreach(confdir,argc-1,argv+1,check_dist,&dat,force);
	r = references_done(dat.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dat.references);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}

/***********************adddeb******************************************/

static int adddeb(int argc,char *argv[]) {
	retvalue result,r;
	DB *files;

	if( argc < 4 ) {
		fprintf(stderr,"mirrorer _adddeb <distribution> <part> <package>\n");
		return 1;
	}

	files = files_initialize(dbdir);
	if( !files )
		return 1;

	//TODO check if distribution is valid and get's its architecture list
	result =deb_add(files,mirrordir,argv[2],argv[1],NULL,argv[3],force);

	r = files_done(files);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}

/*********************/
/* argument handling */
/*********************/

static struct action {
	char *name;
	int (*start)(int argc,char *argv[]);
} actions[] = {
	{"__d", printargs},
	{"_detect", detect},
	{"_forget", forget},
	{"_md5sums", md5sums},
	{"checkrelease",checkrelease},
	{"prepareaddsources", prepareaddsources},
	{"addsources", addsources},
	{"prepareaddpackages", prepareaddpackages},
	{"addpackages", addpackages},
	{"_genpackages", exportpackages},
	{"_genzpackages", zexportpackages},
        {"_removepackage", removepackage},
	{"export", export},
	{"check", check},
	{"rereference", rereference},
	{"_addreference", addreference},
	{"dumpreferences", dumpreferences},
	{"dumpunreferenced", dumpunreferenced},
	{"deleteunreferenced", deleteunreferenced},
	{"_removereferences", removereferences},
	{"_addmd5sums",addmd5sums},
	{"update",update},
	{"__extractcontrol",extract_control},
	{"_adddeb",adddeb},
	{NULL,NULL}
};


int main(int argc,char *argv[]) {
	static struct option longopts[] = {
		{"local", 0, 0, 'l'},
		{"basedir", 1, 0, 'b'},
		{"incommingdir", 1, 0, 'i'},
		{"distdir", 1, 0, 'd'},
		{"dbdir", 1, 0, 'D'},
		{"listdir", 1, 0, 'L'},
		{"confdir", 1, 0, 'c'},
		{"help", 0, 0, 'h'},
		{"verbose", 0, 0, 'v'},
		{"nothingiserror", 0, 0, 'e'},
		{"force", 0, 0, 'f'},
		{0, 0, 0, 0}
	};
	int c;struct action *a;


	while( (c = getopt_long(argc,argv,"+feVvhlb:P:p:d:c:D:L:i:",longopts,NULL)) != -1 ) {
		switch( c ) {
			case 'h':
				printf(
"mirrorer - Manage a debian-mirror\n\n"
"options:\n"
" -h, --help:             Show this help\n"
" -l, --local:            Do only process the given file.\n"
"                         (i.e. do not look at .tar.gz when getting .dsc)\n"
" -m, --mirrordir <dir>:    Base-dir (will overwrite prior given -i, -d, -D).\n"
" -i, --incomming <dir>:  incomming-Directory.\n"
" -d, --distdir <dir>:    Directory to place the \"dists\" dir in.\n"
" -D, --dbdir <dir>:      Directory to place the database in.\n"
" -L, --listdir <dir>:    Directory to place downloaded lists in.\n"
" -c, --confdir <dir>:    Directory to search configuration in.\n"
"\n"
"actions:\n"
" add <filename>:     Not yet implemented.\n"
" _forget <file>:      Forget the given files (read stdin if none)\n"
"                     (Only usefull to unregister files manually deleted)\n"
" _detect <file>:      Add given files to the database (read stdin if none)\n"
"  the following lines are currently wrong...\n"
"  ('find $pooldir -type f -printf \"%%P\\n\" | mirrorer -p $pooldir inventory'\n"
"   will iventory an already existing pool-dir\n"
"   WARNING: names relative to pool-dir in shortest possible form\n"
" _removereferences <identifier>: Remove all marks \"Needed by <identifier>\"\n"
" dumpreferences:    Print all saved references\n"
" dumpunreferenced:   Print registered files withour reference\n"
" deleteunreferenced: Delete and forget all unreferenced files\n"
" export              Generate Packages.gz/Packages/Sources.gz/Release\n"
" addpackages <identifier> <component> <files>:\n"
"       Add the contents of Packages-files <files> to dist <identifier>\n"
" prepareaddpackages <identifier> <component> <files>:\n"
"       Search for missing files and print those not found\n"
" addsources <identifier> <component> <files>:\n"
"       Add the contents of Sources-files <files> to dist <identifier>\n"
" prepareaddsources <identifier> <component> <files>:\n"
"       Search for missing files and print those not found\n"
"\n"
						);
				exit(0);
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
				mirrordir=strdup(optarg);
				incommingdir=mprintf("%s/incomming",optarg);
				distdir=mprintf("%s/dists",optarg);
				dbdir=mprintf("%s/db",optarg);
				listdir=mprintf("%s/lists",optarg);
				confdir=mprintf("%s/conf",optarg);
				break;
			case 'i':
				incommingdir = strdup(optarg);
				break;
			case 'd':
				distdir = strdup(optarg);
				break;
			case 'D':
				dbdir = strdup(optarg);
				break;
			case 'L':
				listdir = strdup(optarg);
				break;
			case 'c':
				confdir = strdup(optarg);
				break;
			default:
				fprintf (stderr,"Not supported option '-%c'\n", c);
				exit(EXIT_FAILURE);
		}
	}
	if( optind >= argc ) {
		fprintf(stderr,"No action given. (see --help for available options and actions)\n");
		exit(EXIT_FAILURE);
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

