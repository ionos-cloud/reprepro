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
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
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

int printargs(int argc,char *argv[]) {
	int i;

	fprintf(stderr,"argc: %d\n",argc);
	for( i=0 ; i < argc ; i++ ) {
		fprintf(stderr,"%s\n",argv[i]);
	}
	return 0;
}


int release(int argc,char *argv[]) {
	DB *refs;

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer release <identifier>\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	if( references_removedepedency(refs,argv[1]) < 0 ) {
		references_done(refs);
		return 1;
	}
	references_done(refs);
	return 0;
}
int addreference(int argc,char *argv[]) {
	DB *refs;
	int result;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer addreference <reference> <referee>\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	result = references_adddepedency(refs,argv[1],argv[2]);
	references_done(refs);
	return result;
}
int exportpackages(int argc,char *argv[]) {
	DB *pkgs;
	int result;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer export <identifier> <Packages-file to create>\n");
		return 1;
	}
	pkgs = packages_initialize(dbdir,argv[1]);
	if( ! pkgs )
		return 1;
	result = packages_printout(pkgs,argv[2]);
	packages_done(pkgs);
	return result;
}
int zexportpackages(int argc,char *argv[]) {
	DB *pkgs;
	int result;

	if( argc != 3 ) {
		fprintf(stderr,"mirrorer zexport <identifier> <Packages-file to create>\n");
		return 1;
	}
	pkgs = packages_initialize(dbdir,argv[1]);
	if( ! pkgs )
		return 1;
	result = packages_zprintout(pkgs,argv[2]);
	packages_done(pkgs);
	return result;
}

/****** common for [prepare]add{sources,packages} *****/

struct distribution {
	DB *files,*pkgs;
	const char *referee,*part;
};

/***********************************addsources***************************/

int add_source(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,int hadold) {
	char *newchunk,*fulldir;
	int r;
	struct distribution *dist = (struct distribution*)data;
	const char *nextfile;
	char *filename,*filekey,*md5andsize;

	/* look for needed files */

	nextfile = files;
	while( (r =sources_getfile(&nextfile,&filename,&md5andsize))>0 ){
		filekey = calc_srcfilekey(directory,filename);
		
		r = files_expect(dist->files,pooldir,filekey,md5andsize);
		if( r < 0 ) {
			free(filename);free(md5andsize);free(filekey);
			return -1;
		}
		if( r == 0 ) {
			/* File missing */
			printf("Missing file %s\n",filekey);
			return -1;
		}

		free(filename);free(md5andsize);free(filekey);
	}

	/* Add package to distribution's database */

	fulldir = calc_fullfilename(ppooldir,directory);
	if( !fulldir )
		return -1;

	newchunk = chunk_replaceentry(chunk,"Directory",fulldir);
	free(fulldir);
	if( !newchunk )
		return -1;
	if( hadold )
		r = packages_replace(dist->pkgs,package,newchunk);
	else
		r = packages_add(dist->pkgs,package,newchunk);
	free(newchunk);
	if( r < 0 )
		return -1;
	return 0;
}

int addsources(int argc,char *argv[]) {
	int i,r,result;
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
		dist.files->close(dist.files,0);
		return 1;
	}
	result = 0;
	for( i=3 ; i < argc ; i++ ) {
		r = sources_add(dist.pkgs,dist.part,argv[i],add_source,&dist);
		if( r < 0 )
			result = r;
	}
	dist.files->close(dist.files,0);
	packages_done(dist.pkgs);
	return result;
}
/****************************prepareaddsources********************************************/

int showmissingsourcefiles(void *data,const char *chunk,const char *package,const char *directory,const char *olddirectory,const char *files,int hadold) {
	int r;
	struct distribution *dist = (struct distribution*)data;
	char *dn;
	const char *nextfile;
	char *filename,*filekey,*md5andsize;

	/* look for directory */
	if( (dn = calc_fullfilename(pooldir,directory))) {
		r = make_dir_recursive(dn);
		free(dn);
		if( r < 0 )
			return -1;
	}

	nextfile = files;
	while( (r =sources_getfile(&nextfile,&filename,&md5andsize))>0 ){
		filekey = calc_srcfilekey(directory,filename);
		
		r = files_expect(dist->files,pooldir,filekey,md5andsize);
		if( r < 0 ) {
			free(filename);free(md5andsize);free(filekey);
			return -1;
		}
		if( r == 0 ) {
			/* File missing */
			printf("%s/%s %s/%s\n",olddirectory,filename,pooldir,filekey);
		}

		free(filename);free(md5andsize);free(filekey);
	}

	return r;
}

int prepareaddsources(int argc,char *argv[]) {
	int i,r,result;
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
		dist.files->close(dist.files,0);
		return 1;
	}
	result = 0;
	for( i=3 ; i < argc ; i++ ) {
		r = sources_add(dist.pkgs,dist.part,argv[i],showmissingsourcefiles,&dist);
		if( r < 0 )
			result = r;
	}
	dist.files->close(dist.files,0);
	packages_done(dist.pkgs);
	return result;
}

/****************************prepareaddpackages*******************************************/

int showmissing(void *data,const char *chunk,const char *package,const char *sourcename,const char *oldfile,const char *filename,const char *filekey,const char *md5andsize,int hadold) {
	int r;
	struct distribution *dist = (struct distribution*)data;
	char *fn;

	/* look for needed files */

	r = files_expect(dist->files,pooldir,filekey,md5andsize);
	if( r < 0 )
		return -1;
	if( r == 0 ) {
		/* look for directory */
		if( (fn = calc_fullfilename(pooldir,filekey))) {
			make_parent_dirs(fn);
			free(fn);
		}
		/* File missing */
		printf("%s %s/%s\n",oldfile,pooldir,filekey);
	}
	return 0;

}

int prepareaddpackages(int argc,char *argv[]) {
	int i,r,result;
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
		dist.files->close(dist.files,0);
		return 1;
	}
	result = 0;
	for( i=3 ; i < argc ; i++ ) {
		r = binaries_add(dist.pkgs,dist.part,argv[i],showmissing,&dist);
		if( r < 0 )
			result = r;
	}
	dist.files->close(dist.files,0);
	packages_done(dist.pkgs);
	return result;
}

/***********************************addpackages*******************************************/

int add_package(void *data,const char *chunk,const char *package,const char *sourcename,const char *oldfile,const char *filename,const char *filekey,const char *md5andsize,int hadold) {
	char *newchunk;
	char *filewithdir;
	int r;
	struct distribution *dist = (struct distribution*)data;

	/* look for needed files */

	if( files_expect(dist->files,pooldir,filekey,md5andsize) <= 0 ) {
		printf("Missing file %s\n",filekey);
		return -1;
	} 

	/* Add package to distribution's database */

	filewithdir = calc_fullfilename(ppooldir,filekey);
	if( !filewithdir )
		return -1;
	newchunk = chunk_replaceentry(chunk,"Filename",filewithdir);
	free(filewithdir);
	if( !newchunk )
		return -1;
	if( hadold )
		r = packages_replace(dist->pkgs,package,newchunk);
	else
		r = packages_add(dist->pkgs,package,newchunk);
	free(newchunk);
	if( r < 0 )
		return -1;
	return 0;
}


int addpackages(int argc,char *argv[]) {
	int i,r,result;
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
		dist.files->close(dist.files,0);
		return 1;
	}
	dist.referee = argv[1];
	dist.part = argv[2];
	result = 0;
	for( i=3 ; i < argc ; i++ ) {
		r = binaries_add(dist.pkgs,dist.part,argv[i],add_package,&dist);
		if( r < 0 )
			result = r;
	}
	dist.files->close(dist.files,0);
	packages_done(dist.pkgs);
	return result;
}





int detect(int argc,char *argv[]) {
	DB *files;
	char buffer[5000],*nl;
	int i,ret;

	ret = 0;
	files = files_initialize(dbdir);
	if( !files )
		return 1;
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			switch(	files_detect(files,pooldir,argv[i]) ) {
				case -2: ret = 10; break;
				case -1: if(ret<2)ret = 2; break;
			}
		}

	} else
		while( fgets(buffer,4999,stdin) ) {
			nl = index(buffer,'\n');
			if( !nl ) {
				files->close(files,0);
				return 1;
			}
			*nl = '\0';
			switch(	files_detect(files,pooldir,buffer) ) {
				case -2: ret = 10; break;
				case -1: if(ret<2)ret = 2; break;
			}
		} 
	files->close(files,0);
	return ret;
}
int forget(int argc,char *argv[]) {
	DB *files;
	char buffer[5000],*nl;
	int i;

	files = files_initialize(dbdir);
	if( !files )
		return 1;
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			files_remove(files,argv[i]);
		}

	} else
		while( fgets(buffer,4999,stdin) ) {
			nl = index(buffer,'\n');
			if( !nl ) {
				files->close(files,0);
				return 1;
			}
			*nl = '\0';
			files_remove(files,buffer);
		} 
	files->close(files,0);
	return 0;
}
int grapdependencies(int argc,char *argv[]) {
	fprintf(stderr,"Not yet implemented!\n");
	return 1;
}

int md5sums(int argc,char *argv[]) {
	DB *files;

	files = files_initialize(dbdir);
	if( !files )
		return 1;
	files_printmd5sums(files);
	files->close(files,0);
	return 0;
}

int checkrelease(int argc,char *argv[]) {
	if( argc != 4 ) {
		fprintf(stderr,"mirrorer checkrelease <Release-file> <name to look up> <Packages or Sources-files to check>\n");
		return 1;
	}
	return release_checkfile(argv[1],argv[2],argv[3]) != 1;
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
	{"release", release},
	{"grapdependencies",grapdependencies},
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
		{"force", 0, 0, 'f'},
		{0, 0, 0, 0}
	};
	int c;struct action *a;


	while( (c = getopt_long(argc,argv,"+fvhlb:P:p:d:D:i:",longopts,NULL)) != -1 ) {
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

