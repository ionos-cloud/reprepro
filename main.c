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
#include "dirs.h"
#include "names.h"
#include "files.h"
#include "packages.h"
#include "reference.h"
#include "chunks.h"
#include "binaries.h"
#include "sources.h"
#include "release.h"

/* global options */
char 	*incommingdir = "/var/spool/mirrorer/incomming",
	*ppooldir = "pool",
	*pooldir = "/var/spool/mirrorer/pool",
	*distdir = "/var/spool/mirrorer/dists",
	*dbdir = "/var/spool/mirrorer/db";
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


int release(int argc,char *argv[]) {
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
	RET_UPDATE(ret,r);
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
	DB *pkgs;
	retvalue result,r;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer export <identifier> <Packages-file to create>\n");
		return 1;
	}
	pkgs = packages_initialize(dbdir,argv[1]);
	if( ! pkgs )
		return 1;
	result = packages_printout(pkgs,argv[2]);
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}
int zexportpackages(int argc,char *argv[]) {
	DB *pkgs;
	retvalue result,r;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer zexport <identifier> <Packages-file to create>\n");
		return 1;
	}
	pkgs = packages_initialize(dbdir,argv[1]);
	if( ! pkgs )
		return 1;
	result = packages_zprintout(pkgs,argv[2]);
	r = packages_done(pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/****** common for reference{binaries,sources} *****/
struct referee {
	DB *pkgs,*refs;
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

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer referencebinaries <identifier>\n");
		return 1;
	}
	dist.identifier = argv[1];
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs )
		return 1;
	dist.pkgs = packages_initialize(dbdir,argv[1]);
	if( ! dist.pkgs ) {
		references_done(dist.refs);
		return 1;
	}
	result = packages_foreach(dist.pkgs,reference_binary,&dist);

	r = packages_done(dist.pkgs);
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

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer referencesources <identifier>\n");
		return 1;
	}
	dist.identifier = argv[1];
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs )
		return 1;
	dist.pkgs = packages_initialize(dbdir,argv[1]);
	if( ! dist.pkgs ) {
		references_done(dist.refs);
		return 1;
	}
	result = packages_foreach(dist.pkgs,reference_source,&dist);

	r = packages_done(dist.pkgs);
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

retvalue add_source(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,int hadold) {
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
	if( hadold )
		r = packages_replace(dist->pkgs,package,newchunk);
	else
		r = packages_add(dist->pkgs,package,newchunk);
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

retvalue showmissingsourcefiles(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,int hadold) {
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

retvalue showmissing(void *data,const char *chunk,const char *package,const char *sourcename,const char *oldfile,const char *filename,const char *filekey,const char *md5andsize,int hadold) {
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

retvalue add_package(void *data,const char *chunk,const char *package,const char *sourcename,const char *oldfile,const char *filename,const char *filekey,const char *md5andsize,int hadold) {
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
		return RET_ERROR;
	newchunk = chunk_replaceentry(chunk,"Filename",filewithdir);
	free(filewithdir);
	if( !newchunk )
		return RET_ERROR;
	if( hadold )
		r = packages_replace(dist->pkgs,package,newchunk);
	else
		r = packages_add(dist->pkgs,package,newchunk);
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
	retvalue ret,r;

	files = files_initialize(dbdir);
	if( !files )
		return 1;
	ret = files_printmd5sums(files);
	r = files_done(files);
	RET_ENDUPDATE(ret,r);
	return EXIT_RET(ret);
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
	{"export", exportpackages},
	{"zexport", zexportpackages},
	{"addreference", addreference},
	{"dumpreferences", dumpreferences},
	{"release", release},
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
		{"help", 0, 0, 'h'},
		{"verbose", 0, 0, 'v'},
		{"nothingiserror", 0, 0, 'e'},
		{"force", 0, 0, 'f'},
		{0, 0, 0, 0}
	};
	int c;struct action *a;


	while( (c = getopt_long(argc,argv,"+fevhlb:P:p:d:D:i:",longopts,NULL)) != -1 ) {
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
"\n"
"actions:\n"
" add <filename>:     Not yet implemented.\n"
" forget <file>:      Forget the given files (read stdin if none)\n"
"                     (Only usefull to unregister files manually deleted)\n"
" detect <file>:      Add given files to the database (read stdin if none)\n"
"  ('find $pooldir -type f -printf \"%%P\\n\" | mirrorer -p $pooldir inventory'\n"
"   will iventory an already existing pool-dir\n"
"   WARNING: names relative to pool-dir in shortest possible form\n"
" release <type>:     Remove all marks \"Needed by <type>\"\n"
" referencebinaries <identifer>:\n"
"       Mark everything in dist <identifier> to be needed by <identifier>\n"
" referencesources <identifer>:\n"
"       Mark everything in dist <identifier> to be needed by <identifier>\n"
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
			case 'e':
				nothingiserror=1;
				break;
			case 'f':
				force++;
				break;
			case 'b':
				asprintf(&incommingdir,"%s/incomming",optarg);
				asprintf(&pooldir,"%s/pool",optarg);
				asprintf(&distdir,"%s/dists",optarg);
				asprintf(&dbdir,"%s/db",optarg);
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

