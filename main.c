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
#include "downloadcache.h"


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
	*methoddir = "/usr/lib/apt/methods" ,
	*section = NULL,
	*priority = NULL,
	*component = NULL,
	*architecture = NULL;
static int	local = 0;
static int	force = 0;
static int	nothingiserror = 0;
int		verbose = 0;

static int printargs(int argc,const char *argv[]) {
	int i;

	fprintf(stderr,"argc: %d\n",argc);
	for( i=0 ; i < argc ; i++ ) {
		fprintf(stderr,"%s\n",argv[i]);
	}
	return 0;
}
static int extract_control(int argc,const char *argv[]) {
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


static int addmd5sums(int argc,const char *argv[]) {
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


static int removereferences(int argc,const char *argv[]) {
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


static int dumpreferences(int argc,const char *argv[]) {
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

static int dumpunreferenced(int argc,const char *argv[]) {
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
			} 
			if( err == 0 || force ) 
				r = files_remove(dist->files,filekey);
			free(filename);
		}
		return r;
	} else if( RET_IS_OK(r) ) {
		return RET_NOTHING;
	} else
		return r;
}

static int deleteunreferenced(int argc,const char *argv[]) {
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

static int addreference(int argc,const char *argv[]) {
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


struct remove_args {DB *references; int count; const char **names; };

static retvalue remove_from_target(void *data, struct target *target) {
	retvalue result,r;
	int i;
	struct remove_args *d = data;

	result = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = RET_NOTHING;
	for( i = 0 ; i < d->count ; i++ ){
		r = target_removepackage(target,d->references,d->names[i]);
		RET_UPDATE(result,r);
	}
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	return result;
}

static int removepackage(int argc,const char *argv[]) {
	retvalue result,r;
	struct distribution *distribution;
	struct remove_args d;

	if( argc < 3  ) {
		fprintf(stderr,"mirrorer [-C <component>] [-A <architecture>] remove <codename> <package-names>\n");
		return 1;
	}
	d.references = references_initialize(dbdir);
	if( ! d.references )
		return 1;
	r = distribution_get(&distribution,confdir,argv[1]);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find matching distributions!\n");
		return EXIT_RET(RET_NOTHING);
	}
	if( RET_WAS_ERROR(r) ) {
		(void)references_done(d.references);
		return EXIT_RET(r);
	}

	d.count = argc-2;
	d.names = argv+2;

	result = distribution_foreach_part(distribution,component,architecture,remove_from_target,&d,force);

	r = distribution_export(distribution,dbdir,distdir,force,1);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	r = references_done(d.references);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

static retvalue list_in_target(void *data, struct target *target) {
	retvalue r,result;
	const char *packagename = data;
	char *control,*version;

	result = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = packages_get(target->packages,packagename,&control);
	if( RET_IS_OK(result) ) {
		r = target->getversion(target,control,&version);
		if( RET_IS_OK(r) ) {
			printf("%s: %s %s\n",target->identifier,packagename,version);
			free(version);
		} else {
			printf("Could not retrieve version from %s in %s\n",packagename,target->identifier);
		}
		free(control);
	}
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	return result;
}

static int listpackage(int argc,const char *argv[]) {
	retvalue r,result;
	struct distribution *distribution;

	if( argc != 3  ) {
		fprintf(stderr,"mirrorer [-C <component>] [-A <architecture>] list <codename> <package-name>\n");
		return 1;
	}
	r = distribution_get(&distribution,confdir,argv[1]);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find matching distributions!\n");
		return EXIT_RET(RET_NOTHING);
	}
	if( RET_WAS_ERROR(r) ) {
		return EXIT_RET(r);
	}

	result = distribution_foreach_part(distribution,component,architecture,list_in_target,(void*)argv[2],force);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	return EXIT_RET(result);
}

static int detect(int argc,const char *argv[]) {
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

static int forget(int argc,const char *argv[]) {
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

static int md5sums(int argc,const char *argv[]) {
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

static retvalue printout(void *data,const char *package,const char *chunk){
	printf("'%s' -> '%s'\n",package,chunk);
	return RET_OK;
}

static int dumpcontents(int argc,const char *argv[]) {
	retvalue result,r;
	packagesdb packages;

	if( argc != 2 ) {
		fprintf(stderr,"mirrorer _dumpcontents <identifier>\n");
		return 1;
	}

	result = packages_initialize(&packages,dbdir,argv[1]);
	if( RET_WAS_ERROR(result) )
		return EXIT_RET(result);

	result = packages_foreach(packages,printout,NULL,force);

	r = packages_done(packages);
	RET_ENDUPDATE(result,r);
	
	return EXIT_RET(result);
}

static retvalue doexport(void *dummy,const char *chunk,struct distribution *distribution) {

	if( verbose > 0 ) {
		fprintf(stderr,"Exporting %s...\n",distribution->codename);
	}

	return distribution_export(distribution,dbdir,distdir,force,0);
}

static int export(int argc,const char *argv[]) {
	retvalue result;

	if( argc < 1 ) {
		fprintf(stderr,"mirrorer export [<distributions>]\n");
		return 1;
	}
	
	result = distribution_foreach(confdir,argc-1,argv+1,doexport,NULL,force);
	return EXIT_RET(result);
}

/***********************update********************************/

static int update(int argc,const char *argv[]) {
	retvalue result,r;
	int doexport;
	DB *refs;
	struct update_pattern *patterns;
	struct distribution *distributions;
	filesdb files;

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
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Nothing to do found!\n");
		return EXIT_RET(RET_NOTHING);
	}

	result = updates_getpatterns(confdir,&patterns,0);
	if( RET_WAS_ERROR(result) )
		return EXIT_RET(result);

	result = updates_getindices(listdir,patterns,distributions);
	if( RET_WAS_ERROR(result) )
		return EXIT_RET(result);

	refs = references_initialize(dbdir);
	if( ! refs )
		return 1;
	result = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(result) ) {
		references_done(refs);
		return EXIT_RET(result);
	}

	result = updates_update(dbdir,listdir,methoddir,files,refs,distributions,force);

	r = files_done(files);
	RET_ENDUPDATE(result,r);
	
	doexport = force || RET_IS_OK(result);
	if( doexport && verbose >= 0 )
		fprintf(stderr,"Exporting indices...\n");
	while( distributions ) {
		struct distribution *d = distributions->next;

		if( doexport ) {
			r = distribution_export(distributions,dbdir,distdir,force,1);
			RET_ENDUPDATE(result,r);
		}
		
		distribution_free(distributions);

		distributions = d;
	}

	references_done(refs);

	return EXIT_RET(result);
}


/***********************rereferencing*************************/
struct data_binsrcreref { const struct distribution *distribution; DB *references;};

static retvalue reref(void *data,struct target *target) {
	retvalue result,r;
	struct data_binsrcreref *d = data;

	result = target_initpackagesdb(target,dbdir);
	if( !RET_WAS_ERROR(result) ) {
		result = target_rereference(target,d->references,force);
		r = target_closepackagesdb(target);
		RET_ENDUPDATE(result,r);
	}
	return result;
}


static retvalue rereference_dist(void *data,const char *chunk,struct distribution *distribution) {
	struct data_binsrcreref dat;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Referencing %s...\n",distribution->codename);
	}

	dat.distribution = distribution;
	dat.references = data;

	result = distribution_foreach_part(distribution,NULL,NULL,reref,&dat,force);

	return result;
}

static int rereference(int argc,const char *argv[]) {
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
	retvalue result,r;

	r = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	result = target_check(target,d->files,d->references,force);
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	return result;
}

static retvalue check_dist(void *data,const char *chunk,struct distribution *distribution) {
	struct data_check *dat=data;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Checking %s...\n",distribution->codename);
	}


	dat->distribution = distribution;

	result = distribution_foreach_part(distribution,component,architecture,check_target,dat,force);
	
	return result;
}

static int check(int argc,const char *argv[]) {
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

static int checkpool(int argc,const char *argv[]) {
	retvalue result,r;
	filesdb files;

	if( argc < 1 || argc > 2 || (argc == 2 && strcmp(argv[1],"fast") != 0)) {
		fprintf(stderr,"mirrorer checkpool [fast] \n");
		return 1;
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		return EXIT_RET(r);
	}

	result = files_checkpool(files,argc == 2);
	
	r = files_done(files);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}

/***********************include******************************************/

static int includedeb(int argc,const char *argv[]) {
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

	if( architecture && !strlist_in(&distribution->architectures,architecture) ){
		fprintf(stderr,"Cannot force into the architecture '%s' not available in '%s'!\n",architecture,distribution->codename);
		return 2;
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return EXIT_RET(r);
	references = references_initialize(dbdir);
	if( !references )
		return 1;

	result = deb_add(dbdir,references,files,component,architecture,section,priority,distribution,argv[2],NULL,NULL,force);

	r = distribution_export(distribution,dbdir,distdir,force,1);
	RET_ENDUPDATE(result,r);

	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	r = files_done(files);
	RET_ENDUPDATE(result,r);
	r = references_done(references);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}


static int includedsc(int argc,const char *argv[]) {
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
	
	r = distribution_export(distribution,dbdir,distdir,force,1);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	r = files_done(files);
	RET_ENDUPDATE(result,r);
	r = references_done(references);
	RET_ENDUPDATE(result,r);

	return EXIT_RET(result);
}

static int includechanges(int argc,const char *argv[]) {
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
	
	r = distribution_export(distribution,dbdir,distdir,force,1);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
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
	int (*start)(int argc,const char *argv[]);
} actions[] = {
	{"__d", printargs},
	{"_detect", detect},
	{"_forget", forget},
	{"_md5sums", md5sums},
	{"_dumpcontents", dumpcontents},
        {"remove", removepackage},
	{"list", listpackage},
	{"export", export},
	{"check", check},
	{"checkpool", checkpool},
	{"rereference", rereference},
	{"_addreference", addreference},
	{"dumpreferences", dumpreferences},
	{"dumpunreferenced", dumpunreferenced},
	{"deleteunreferenced", deleteunreferenced},
	{"_removereferences", removereferences},
	{"_addmd5sums",addmd5sums},
	{"update",update},
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
		{"methoddir", 1, 0, 'M'},
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


	while( (c = getopt_long(argc,argv,"+feVvhlb:P:p:d:c:D:L:M:i:A:C:S:",longopts,NULL)) != -1 ) {
		switch( c ) {
			case 'h':
				printf(
"mirrorer - Manage a debian-mirror\n\n"
"options:\n"
" -h, --help:             Show this help\n"
//" -l, --local:            Do only process the given file.\n"
//"                         (i.e. do not look at .tar.gz when getting .dsc)\n"
" -b, --basedir <dir>:    Base-dir (will overwrite prior given\n"
"                                   -d, -D, -L, -c).\n"
// TODO: is not yet used...
// " -i, --incomming <dir>:  incomming-Directory.\n"
" -d, --distdir <dir>:    Directory to place the \"dists\" dir in.\n"
" -D, --dbdir <dir>:      Directory to place the database in.\n"
" -L, --listdir <dir>:    Directory to place downloaded lists in.\n"
" -c, --confdir <dir>:    Directory to search configuration in.\n"
" -M, --methodir <dir>:   Use instead of /usr/lib/apt/methods/\n"
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
" checkpool:          Check if all files in the pool are still in proper shape.\n"
" check [<distributions>]\n"
"       Check for all needed files to be registered properly.\n"
" export [<distributions>]\n"
"	Force (re)generation of Packages.gz/Packages/Sources.gz/Release\n"
" update [<distributions>]\n"
"	Update the given distributions from the configured sources.\n"
" remove <distribution> <packagename>\n"
"       Remove the given package from the specified distribution.\n"
" include <distribution> <.changes-file>\n"
"       Inlcude the given upload.\n"
" includedeb <distribution> <.deb-file>\n"
"       Inlcude the given binary package.\n"
" includedsc <distribution> <.dsc-file>\n"
"       Inlcude the given source package.\n"
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
			case 'M':
				methoddir = strdup(optarg);
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
			return a->start(argc-optind,(const char**)argv+optind);
		} else
			a++;
	}

	fprintf(stderr,"Unknown action '%s'. (see --help for available options and actions)\n",argv[optind]);
	return 2;
}

