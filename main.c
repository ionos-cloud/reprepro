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
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "dirs.h"
#include "names.h"
#include "md5sum.h"
#include "chunks.h"
#include "files.h"
#include "target.h"
#include "packages.h"
#include "reference.h"
#include "binaries.h"
#include "sources.h"
#include "release.h"
#include "aptmethod.h"
#include "updates.h"
#include "upgradelist.h"
#include "signature.h"
#include "extractcontrol.h"
#include "checkindeb.h"
#include "checkindsc.h"
#include "checkin.h"


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
	*confdir = STD_BASE_DIR "/conf",
	*section = NULL,
	*priority = NULL,
	*component = NULL,
	*architecture = NULL;
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
	filesdb files;
	retvalue result,r;

	if( argc != 1 ) {
		fprintf(stderr,"mirrorer _addmd5sums < <data>\n");
		return 1;
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return EXIT_RET(r);

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

struct fileref { filesdb files; DB *refs; };

static retvalue checkifreferenced(void *data,const char *filekey,const char *md5sum) {
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
	r = files_initialize(&dist.files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		(void)references_done(dist.refs);
		return EXIT_RET(r);
	}
	result = files_foreach(dist.files,checkifreferenced,&dist);
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

static retvalue deleteifunreferenced(void *data,const char *filekey,const char *md5sum) {
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
	r = files_initialize(&dist.files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		(void)references_done(dist.refs);
		return EXIT_RET(r);
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

static int removepackage(int argc,char *argv[]) {
	retvalue result,r;
	DB *refs;
	int i;
	struct distribution *distribution;
	struct target *target;

	//TODO: add architecture-selector...

	if( argc < 3 || component == NULL || architecture == NULL ) {
		fprintf(stderr,"mirrorer -C <component> -A <architecture> removebinary <codename> <package-names>\n");
		return 1;
	}
	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	r = distribution_get(&distribution,confdir,argv[1]);
	if( RET_WAS_ERROR(r) ) {
		(void)references_done(refs);
		return EXIT_RET(r);
	}

	if( !strlist_in(&distribution->components,component) ) {
		fprintf(stderr,"No component '%s' in '%s'!\n",component,distribution->codename);
		(void)references_done(refs);
		distribution_free(distribution);
		return 1;
	}
	if( !strlist_in(&distribution->architectures,architecture) ) {
		fprintf(stderr,"No architecture '%s' in '%s'!\n",architecture,distribution->codename);
		(void)references_done(refs);
		distribution_free(distribution);
		return 1;
	}

	target = distribution_getpart(distribution,component,architecture);

	result = target_initpackagesdb(target,dbdir,NULL);
	if( RET_WAS_ERROR(result) ) {
		(void)references_done(refs);
		distribution_free(distribution);
		return 1;
	}

	result = RET_NOTHING;
	for( i = 2 ; i< argc ; i++ ) {
		r = target_removepackage(target,refs,argv[i]);
		RET_UPDATE(result,r);
	}

	distribution_free(distribution);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/****** common for [prepare]add{sources,packages} *****/
//TODO: all these 4 functions are depreceated, they will only
//stay until their replacement works..

struct distributionhandles {
	filesdb files;packagesdb pkgs;DB *refs;
	const char *referee,*component;
};

/***********************************addsources***************************/

static retvalue add(void *data,const char *chunk,const char *package,const char *version,const struct strlist *filekeys,const struct strlist *origfiles,const struct strlist *md5sums,const struct strlist *oldfilekeys) {
	retvalue result;
	struct distributionhandles *dist = (struct distributionhandles*)data;

	/* Add package to distribution's database */

	result = files_expectfiles(dist->files,filekeys,md5sums);
	if( RET_WAS_ERROR(result) )
		return result;

	result = packages_insert(dist->refs,dist->pkgs,
			package,chunk,filekeys,oldfilekeys);

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
	r = files_initialize(&dist.files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		return EXIT_RET(r);
	}
	r = packages_initialize(&dist.pkgs,dbdir,dist.referee);
	if( RET_WAS_ERROR(r) ) {
		(void)files_done(dist.files);
		return EXIT_RET(r);
	}
	dist.refs = references_initialize(dbdir);
	if( ! dist.refs ) {
		(void)files_done(dist.files);
		(void)packages_done(dist.pkgs);
		return 1;
	}
	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = sources_findnew(dist.pkgs,dist.component,argv[i],add,&dist,force);
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

static retvalue showmissing(void *data,const char *chunk,const char *package,const char *version,const struct strlist *filekeys,const struct strlist *origfiles,const struct strlist *md5sums,const struct strlist *oldfilekeys) {
	retvalue r,ret;
	struct distributionhandles *dist = (struct distributionhandles*)data;

	// This is a bit stupid, first to generate the filekeys and
	// then splitting the directory from it. But this should go
	// away, when updating is solved a bit more reasoable...
	r = dirs_make_parents(mirrordir,filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	ret = files_printmissing(dist->files,filekeys,md5sums,origfiles);
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

	r = files_initialize(&dist.files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		return EXIT_RET(r);
	}
	r = packages_initialize(&dist.pkgs,dbdir,dist.referee);
	if( RET_WAS_ERROR(r) ) {
		(void)files_done(dist.files);
		return EXIT_RET(r);
	}

	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = sources_findnew(dist.pkgs,dist.component,argv[i],showmissing,&dist,force);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/****************************prepareaddpackages*******************************************/

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

	r = files_initialize(&dist.files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		return EXIT_RET(r);
	}
	r = packages_initialize(&dist.pkgs,dbdir,dist.referee);
	if( RET_WAS_ERROR(r) ) {
		(void)files_done(dist.files);
		return EXIT_RET(r);
	}

	dist.refs = NULL;

	result = RET_NOTHING;
	for( i=3 ; i < argc ; i++ ) {
		r = binaries_findnew(dist.pkgs,dist.component,argv[i],showmissing,&dist,force);
		RET_UPDATE(result,r);
	}
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = packages_done(dist.pkgs);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

/***********************************addpackages*******************************************/

static int addpackages(int argc,char *argv[]) {
	int i;
	retvalue r,result;
	struct distributionhandles dist;

	if( argc <= 3 ) {
		fprintf(stderr,"mirrorer addpackages <identifier> <component> <Packages-files>\n");
		return 1;
	}
	r = files_initialize(&dist.files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		return EXIT_RET(r);
	}
	r = packages_initialize(&dist.pkgs,dbdir,argv[1]);
	if( RET_WAS_ERROR(r) ) {
		(void)files_done(dist.files);
		return EXIT_RET(r);
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
		r = binaries_findnew(dist.pkgs,dist.component,argv[i],add,&dist,force);
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
	filesdb files;
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	ret = RET_NOTHING;
	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return EXIT_RET(r);
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			r = files_detect(files,argv[i]);
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
			r = files_detect(files,buffer);
			RET_UPDATE(ret,r);
		} 
	r = files_done(files);
	RET_ENDUPDATE(ret,r);
	return EXIT_RET(ret);
}

static int forget(int argc,char *argv[]) {
	filesdb files;
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return EXIT_RET(r);
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
	filesdb files;
	char *filename,*md5sum;
	retvalue ret,r;
	int i;

	if( argc > 1 ) {
		ret = RET_NOTHING;
		for( i = 1 ; i < argc ; i++ ) {
			filename=calc_dirconcat(distdir,argv[i]);
			r = md5sum_read(filename,&md5sum);
			RET_UPDATE(ret,r);
			if( RET_IS_OK(r) ) {
				printf(" %s %s\n",md5sum,argv[i]);
				free(md5sum);
				free(filename);
			} else {
				fprintf(stderr,"Error accessing file: %s\n",filename);
				free(filename);
				if( ! force )
					return 1;
			}
		}
		return EXIT_RET(ret);
	} else {
		r = files_initialize(&files,dbdir,mirrordir);
		if( RET_WAS_ERROR(r) )
			return EXIT_RET(r);
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

struct data_export { const struct distribution *distribution;int force;};

static retvalue exportbinsrc(void *data,struct target *target) {
	retvalue result,r;
	struct data_export *d = data;

	result = release_genrelease(d->distribution,target,distdir);

	if( RET_WAS_ERROR(result) && !force )
		return result;

	r = target_initpackagesdb(target,dbdir,NULL);
	if( !RET_WAS_ERROR(r) )
		r = target_export(target,distdir,d->force);
	RET_UPDATE(result,r);

	return result;
}

static retvalue doexport(void *dummy,const char *chunk,const struct distribution *distribution) {
	struct data_export dat;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Exporting %s...\n",distribution->codename);
	}

	dat.distribution = distribution;
	dat.force = force;

	result = distribution_foreach_part(distribution,exportbinsrc,&dat,force);
	if( !RET_WAS_ERROR(result) || force ) {
		retvalue r;

		r = release_gen(distribution,distdir,chunk,force);
		RET_UPDATE(result,r);
	}

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

static int update(int argc,char *argv[]) {
	retvalue result,r;
	struct update_upstream *patterns,*upstreams;
	struct distribution *distributions;
	struct aptmethodrun *run;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer update [<distributions>]\n");
		return 1;
	}

	result = dirs_make_recursive(listdir);	
	if( RET_WAS_ERROR(result) ) {
		return EXIT_RET(result);
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	if( RET_WAS_ERROR(result) )
		return EXIT_RET(result);

	result = updates_getpatterns(confdir,&patterns,0);
	if( RET_WAS_ERROR(result) )
		return EXIT_RET(result);

	result = updates_getupstreams(patterns,distributions,&upstreams);
	if( RET_WAS_ERROR(result) )
		return EXIT_RET(result);

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	r = updates_queuelists(run,listdir,upstreams);

	if( RET_WAS_ERROR(r) ) {
		aptmethod_cancel(run);
		return r;
	}

	result = aptmethod_download(run,"/usr/lib/apt/methods");
	
	if( RET_WAS_ERROR(result) )
		return result;

	r = updates_checklists(listdir,upstreams,force);

	return EXIT_RET(result);
}

static int upgrade(int argc,char *argv[]) {
	retvalue result,r;
	upgradelist upgrade;
	filesdb files;
	struct target *target;

	if( argc <=1 ) {
		fprintf(stderr,"mirrorer upgrade [<distributions>]\n");
		return 1;
	}

	result = dirs_make_recursive(listdir);	
	if( RET_WAS_ERROR(result) ) {
		return EXIT_RET(result);
	}

	r = target_initialize_source("woody","main",&target);
	if( RET_WAS_ERROR(r) ) {
		return EXIT_RET(r);
	}

	result = upgradelist_initialize(&upgrade,target,dbdir,ud_always);
	if( RET_WAS_ERROR(result) ) {
		target_free(target);
		return EXIT_RET(result);
	}

	result = upgradelist_update(upgrade,argv[1],force);
	upgradelist_dump(upgrade);

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_IS_OK(r) ) {
		upgradelist_listmissing(upgrade,files);

		files_done(files);
	}

	r = upgradelist_done(upgrade);
	RET_ENDUPDATE(result,r);
	
	return EXIT_RET(result);
}

/***********************rereferencing*************************/
struct data_binsrcreref { const struct distribution *distribution; DB *references;};

static retvalue reref(void *data,struct target *target) {
	retvalue result;
	struct data_binsrcreref *d = data;

	result = target_initpackagesdb(target,dbdir,NULL);
	if( !RET_WAS_ERROR(result) )
		result = target_rereference(target,d->references,force);
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

	result = distribution_foreach_part(distribution,reref,&dat,force);

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
struct data_check { const struct distribution *distribution; DB *references; filesdb files;};

static retvalue check_target(void *data,struct target *target) {
	struct data_check *d = data;
	retvalue r;

	r = target_initpackagesdb(target,dbdir,NULL);
	if( RET_WAS_ERROR(r) )
		return r;
	return target_check(target,d->files,d->references,force);
}

static retvalue check_dist(void *data,const char *chunk,const struct distribution *distribution) {
	struct data_check *dat=data;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Checking %s...\n",distribution->codename);
	}


	dat->distribution = distribution;

	result = distribution_foreach_part(distribution,check_target,dat,force);
	
	return result;
}

static int check(int argc,char *argv[]) {
	retvalue result,r;
	struct data_check dat;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer check [<distributions>]\n");
		return 1;
	}

	dat.references = references_initialize(dbdir);

	if( ! dat.references )
		return 1;

	r = files_initialize(&dat.files,dbdir,mirrordir);

	if( RET_WAS_ERROR(r) ) {
		(void)references_done(dat.references);
		return EXIT_RET(r);
	}
	
	result = distribution_foreach(confdir,argc-1,argv+1,check_dist,&dat,force);
	r = files_done(dat.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dat.references);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}

/***********************include******************************************/

static int includedeb(int argc,char *argv[]) {
	retvalue result,r;
	filesdb files;DB *references;
	struct distribution *distribution;

	if( argc < 3 ) {
		fprintf(stderr,"mirrorer includedeb <distribution> <package>\n");
		return 1;
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Could not find '%s' in '%s/distributions'!\n",argv[1],confdir);
		return 2;
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return EXIT_RET(r);
	references = references_initialize(dbdir);
	if( !references )
		return 1;

	result = deb_add(dbdir,references,files,component,architecture,section,priority,distribution,argv[2],NULL,NULL,force);
	distribution_free(distribution);

	r = files_done(files);
	RET_ENDUPDATE(result,r);
	r = references_done(references);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}


static int includedsc(int argc,char *argv[]) {
	retvalue result,r;
	filesdb files; DB *references;
	struct distribution *distribution;

	if( argc < 3 ) {
		fprintf(stderr,"mirrorer includedsc <distribution> <package>\n");
		return 1;
	}

	if( architecture && strcmp(architecture,"source") != 0 ) {
		fprintf(stderr,"Cannot put a source-package anywhere else than in architecture 'source'!\n");
		return 2;
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Could not find '%s' in '%s/distributions'!\n",argv[1],confdir);
		return 2;
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( !files )
		return 1;
	references = references_initialize(dbdir);
	if( !files )
		return 1;

	result = dsc_add(dbdir,references,files,component,section,priority,distribution,argv[2],NULL,NULL,NULL,NULL,force);
	distribution_free(distribution);

	r = files_done(files);
	RET_ENDUPDATE(result,r);
	r = references_done(references);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}

static int includechanges(int argc,char *argv[]) {
	retvalue result,r;
	filesdb files;DB *references;
	struct distribution *distribution;

	if( argc < 3 ) {
		fprintf(stderr,"mirrorer include <distribution> <package>\n");
		return 1;
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Could not find '%s' in '%s/distributions'!\n",argv[1],confdir);
		return 2;
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return EXIT_RET(r);
	references = references_initialize(dbdir);
	if( !files )
		return 1;

	result = changes_add(dbdir,references,files,component,architecture,section,priority,distribution,argv[2],force);
	distribution_free(distribution);

	r = files_done(files);
	RET_ENDUPDATE(result,r);
	r = references_done(references);
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
        {"remove", removepackage},
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
	{"upgrade",upgrade},
	{"__extractcontrol",extract_control},
	{"includedeb",includedeb},
	{"includedsc",includedsc},
	{"include",includechanges},
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
		{"section", 1, 0, 'S'},
		{"priority", 1, 0, 'P'},
		{"component", 1, 0, 'C'},
		{"architecture", 1, 0, 'A'},
		{"help", 0, 0, 'h'},
		{"verbose", 0, 0, 'v'},
		{"nothingiserror", 0, 0, 'e'},
		{"force", 0, 0, 'f'},
		{0, 0, 0, 0}
	};
	int c;struct action *a;


	while( (c = getopt_long(argc,argv,"+feVvhlb:P:p:d:c:D:L:i:A:C:S:",longopts,NULL)) != -1 ) {
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
" -S, --section <section>: Force include* to set section.\n"
" -P, --priority <priority>: Force include* to set priority.\n"
" -C, --component <component>: Add or delete only in component.\n"
" -A, --architecture <architecture>: Add or delete only to architecture.\n"
"\n"
"actions:\n"
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
				incommingdir=calc_dirconcat(optarg,"incomming");
				distdir=calc_dirconcat(optarg,"dists");
				dbdir=calc_dirconcat(optarg,"db");
				listdir=calc_dirconcat(optarg,"lists");
				confdir=calc_dirconcat(optarg,"conf");
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
			case 'C':
				component = strdup(optarg);
				break;
			case 'A':
				architecture = strdup(optarg);
				break;
			case 'S':
				section = strdup(optarg);
				break;
			case 'P':
				priority = strdup(optarg);
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

