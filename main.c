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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include "error.h"
#define DEFINE_IGNORE_VARIABLES
#include "ignore.h"
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
#include "terms.h"


#ifndef STD_BASE_DIR
#define STD_BASE_DIR "/var/spool/reprepro"
#endif

/* global options */
static 
char 	*mirrordir = STD_BASE_DIR ,
	*distdir = STD_BASE_DIR "/dists",
	*dbdir = STD_BASE_DIR "/db",
	*listdir = STD_BASE_DIR "/lists",
	*confdir = STD_BASE_DIR "/conf",
	*overridedir = STD_BASE_DIR "/override",
	*methoddir = "/usr/lib/apt/methods" ,
	*section = NULL,
	*priority = NULL,
	*component = NULL,
	*architecture = NULL,
	*packagetype = NULL;
static int	delete = D_COPY;
static int	force = 0;
static bool_t	nothingiserror = FALSE;
static bool_t	nolistsdownload = FALSE;
static bool_t	keepunreferenced = FALSE;
static bool_t	keepunneededlists = FALSE;
static bool_t	onlyacceptsigned = FALSE;
int		verbose = 0;

static inline retvalue removeunreferencedfiles(references refs,filesdb files,struct strlist *dereferencedfilekeys) {
	int i;
	retvalue result,r;
	result = RET_OK;

	for( i = 0 ; i < dereferencedfilekeys->count ; i++ ) {
		const char *filekey = dereferencedfilekeys->values[i];

		r = references_isused(refs,filekey);
		if( r == RET_NOTHING ) {
			r = files_deleteandremove(files,filekey);
		}
		RET_UPDATE(result,r);
	}
	return result;
}

static inline retvalue possiblyremoveunreferencedfilekeys(retvalue result,
		references refs,filesdb files,struct strlist *dereferencedfilekeys) {
	retvalue r;

	if( !keepunreferenced && dereferencedfilekeys->count > 0 ) {
		if( RET_IS_OK(result) ) {
			if( verbose >= 0 )
				fprintf(stderr,"Deleting files no longer referenced...\n");
			r = removeunreferencedfiles(refs,files,dereferencedfilekeys);
			RET_UPDATE(result,r);
		} else {
			fprintf(stderr,
"Not deleting possibly left over files due to previous errors.\n"
"(To avoid the files in the still existing index files vanishing)\n"
"Use dumpunreferenced/deleteunreferenced to show/delete files without referenes.\n");
		}
	}
	strlist_done(dereferencedfilekeys);
	return result;
}

static retvalue action_printargs(int argc,const char *argv[]) {
	int i;

	fprintf(stderr,"argc: %d\n",argc);
	for( i=0 ; i < argc ; i++ ) {
		fprintf(stderr,"%s\n",argv[i]);
	}
	return 0;
}
static retvalue action_extractcontrol(int argc,const char *argv[]) {
	retvalue result;
	char *control;

	if( argc != 2 ) {
		fprintf(stderr,"reprepro __extractcontrol <.deb-file>\n");
		return RET_ERROR;
	}

	result = extractcontrol(&control,argv[1]);
	
	if( RET_IS_OK(result) ) 
		printf("%s\n",control);
	return result;
}


static retvalue action_addmd5sums(int argc,const char *argv[] UNUSED) {
	char buffer[2000],*c,*m;
	filesdb files;
	retvalue result,r;

	if( argc != 1 ) {
		fprintf(stderr,"reprepro _addmd5sums < <data>\n");
		return RET_ERROR;
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;
	
	while( fgets(buffer,1999,stdin) != NULL ) {
		c = index(buffer,'\n');
		if( ! c ) {
			fprintf(stderr,"Line too long\n");
			(void)files_done(files);
			return RET_ERROR;
		}
		*c = '\0';
		m = index(buffer,' ');
		if( ! m ) {
			fprintf(stderr,"Malformed line\n");
			(void)files_done(files);
			return RET_ERROR;
		}
		*m = '\0'; m++;
		r = files_add(files,buffer,m);
		RET_UPDATE(result,r);

	}
	r = files_done(files);
	RET_ENDUPDATE(result,r);
	return result;
}


static retvalue action_removereferences(int argc,const char *argv[]) {
	references refs;
	retvalue ret,r;

	if( argc != 2 ) {
		fprintf(stderr,"reprepro _removereferences <identifier>\n");
		return RET_ERROR;
	}
	r = references_initialize(&refs,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	ret = references_remove(refs,argv[1]);
	r = references_done(refs);
	RET_ENDUPDATE(ret,r);
	return ret;
}


static retvalue action_dumpreferences(int argc,const char *argv[] UNUSED) {
	references refs;
	retvalue result,r;

	if( argc != 1 ) {
		fprintf(stderr,"reprepro dumpreferences\n");
		return RET_ERROR;
	}
	r = references_initialize(&refs,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	result = references_dump(refs);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);
	return result;
}

struct fileref { filesdb files; references refs; };

static retvalue checkifreferenced(void *data,const char *filekey,const char *md5sum UNUSED) {
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

static retvalue action_dumpunreferenced(int argc,const char *argv[] UNUSED) {
	retvalue result,r;
	struct fileref dist;

	if( argc != 1 ) {
		fprintf(stderr,"reprepro dumpunreferenced\n");
		return RET_ERROR;
	}
	r = references_initialize(&dist.refs,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	r = files_initialize(&dist.files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		(void)references_done(dist.refs);
		return r;
	}
	result = files_foreach(dist.files,checkifreferenced,&dist);
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return result;
}

static retvalue deleteifunreferenced(void *data,const char *filekey,const char *md5sum UNUSED) {
	struct fileref *dist = data;
	retvalue r;

	r = references_isused(dist->refs,filekey);
	if( r == RET_NOTHING ) {
		r = files_deleteandremove(dist->files,filekey);
		return r;
	} else if( RET_IS_OK(r) ) {
		return RET_NOTHING;
	} else
		return r;
}

static retvalue action_deleteunreferenced(int argc,const char *argv[] UNUSED) {
	retvalue result,r;
	struct fileref dist;

	if( argc != 1 ) {
		fprintf(stderr,"reprepro deleteunreferenced\n");
		return RET_ERROR;
	}
	if( keepunreferenced ) {
		fprintf(stderr,"Calling deleteunreferenced with --keepunreferencedfiles does not really make sense, does it?\n");
		return RET_ERROR;
	}
	r = references_initialize(&dist.refs,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	r = files_initialize(&dist.files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		(void)references_done(dist.refs);
		return r;
	}
	result = files_foreach(dist.files,deleteifunreferenced,&dist);
	r = files_done(dist.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dist.refs);
	RET_ENDUPDATE(result,r);
	return result;
}

static retvalue action_addreference(int argc,const char *argv[]) {
	references refs;
	retvalue result,r;

	if( argc != 3 ) {
		fprintf(stderr,"reprepro _addreference <reference> <referee>\n");
		return RET_ERROR;
	}
	r = references_initialize(&refs,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	result = references_increment(refs,argv[1],argv[2]);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);
	return result;
}


struct remove_args {references refs; int count; const char **names; bool_t *gotremoved; int todo;struct strlist removedfiles;};

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
		r = target_removepackage(target,d->refs,d->names[i],&d->removedfiles);
		if( RET_IS_OK(r) ) {
			if( ! d->gotremoved[i] )
				d->todo--;
			d->gotremoved[i] = TRUE;
		}
		RET_UPDATE(result,r);
	}
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	return result;
}

static retvalue action_remove(int argc,const char *argv[]) {
	retvalue result,r;
	struct distribution *distribution;
	struct remove_args d;

	if( argc < 3  ) {
		fprintf(stderr,"reprepro [-C <component>] [-A <architecture>] [-T <type>] remove <codename> <package-names>\n");
		return RET_ERROR;
	}
	r = references_initialize(&d.refs,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	r = distribution_get(&distribution,confdir,argv[1]);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find matching distributions!\n");
		return RET_NOTHING;
	}
	if( RET_WAS_ERROR(r) ) {
		(void)references_done(d.refs);
		return r;
	}

	r = strlist_init(&d.removedfiles);
	if( RET_WAS_ERROR(r) ) {
		(void)references_done(d.refs);
		distribution_free(distribution);
		return r;
	}

	d.count = argc-2;
	d.names = argv+2;
	d.todo = d.count;
	d.gotremoved = calloc(d.count,sizeof(*d.gotremoved));
	if( d.gotremoved == NULL )
		result = RET_ERROR_OOM;
	else
		result = distribution_foreach_part(distribution,component,architecture,packagetype,remove_from_target,&d,force);

	if( d.todo < d.count ) {
		r = distribution_export(distribution,dbdir,distdir,force,TRUE);
		RET_ENDUPDATE(result,r);
	}
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	if( !keepunreferenced && d.removedfiles.count > 0 ) {
		filesdb files;

		if( verbose >= 0 )
			fprintf(stderr,"Deleting files no longer referenced...\n");

		r = files_initialize(&files,dbdir,mirrordir);
		RET_ENDUPDATE(result,r);
		if( RET_IS_OK(r) ) {
			r = removeunreferencedfiles(d.refs,files,&d.removedfiles);
			RET_ENDUPDATE(result,r);
			r = files_done(files);
			RET_ENDUPDATE(result,r);
		} else {
			strlist_done(&d.removedfiles);
		}
	}
	r = references_done(d.refs);
	RET_ENDUPDATE(result,r);
	if( verbose >= 0 && !RET_WAS_ERROR(result) && d.todo > 0 ) {
		(void)fputs("Not removed as not found: ",stderr);
		while( d.count > 0 ) {
			d.count--;
			if( ! d.gotremoved[d.count] ) {
				(void)fputs(d.names[d.count],stderr);
				d.todo--;
				if( d.todo > 0 )
					(void)fputs(", ",stderr);
			}
		}
		(void)fputc('\n',stderr);
	}
	return result;
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
		r = (*target->getversion)(target,control,&version);
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

static retvalue action_list(int argc,const char *argv[]) {
	retvalue r,result;
	struct distribution *distribution;

	if( argc != 3  ) {
		fprintf(stderr,"reprepro [-C <component>] [-A <architecture>] [-T <type>] list <codename> <package-name>\n");
		return RET_ERROR;
	}
	r = distribution_get(&distribution,confdir,argv[1]);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find matching distributions!\n");
		return RET_NOTHING;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	result = distribution_foreach_part(distribution,component,architecture,packagetype,list_in_target,(void*)argv[2],force);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	return result;
}


struct listfilter { struct target *target; term *condition; };

static retvalue listfilterprint(void *data,const char *packagename,const char *control){
	struct listfilter *d = data;
	char *version;
	retvalue r;

	r = term_decidechunk(d->condition,control);
	if( RET_IS_OK(r) ) {
		r = (*d->target->getversion)(d->target,control,&version);
		if( RET_IS_OK(r) ) {
			printf("%s: %s %s\n",d->target->identifier,packagename,version);
			free(version);
		} else {
			printf("Could not retrieve version from %s in %s\n",packagename,d->target->identifier);
		}
	}
	return r;
}

static retvalue listfilter_in_target(void *data, struct target *target) {
	retvalue r,result;
	struct listfilter d;

	result = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	d.target = target;
	d.condition = data;
	result = packages_foreach(target->packages,listfilterprint,&d,force);

	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	return result;
}

static retvalue action_listfilter(int argc,const char *argv[]) {
	retvalue r,result;
	struct distribution *distribution;
	term *condition;

	if( argc != 3  ) {
		fprintf(stderr,"reprepro [-C <component>] [-A <architecture>] [-T <type>] listfilter <codename> <term to describe which packages to list>\n");
		return RET_ERROR;
	}
	r = distribution_get(&distribution,confdir,argv[1]);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Did not find matching distributions!\n");
		return RET_NOTHING;
	}
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	result = term_compile(&condition,argv[2],T_OR|T_BRACKETS|T_NEGATION|T_VERSION|T_NOTEQUAL);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_free(distribution);
		RET_ENDUPDATE(result,r);
		return result;
	}

	result = distribution_foreach_part(distribution,component,architecture,packagetype,listfilter_in_target,condition,force);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	return result;
}

static retvalue action_detect(int argc,const char *argv[]) {
	filesdb files;
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	ret = RET_NOTHING;
	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return r;
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
				return RET_ERROR;
			}
			*nl = '\0';
			r = files_detect(files,buffer);
			RET_UPDATE(ret,r);
		} 
	r = files_done(files);
	RET_ENDUPDATE(ret,r);
	return ret;
}

static retvalue action_forget(int argc,const char *argv[]) {
	filesdb files;
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return r;
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
				return RET_ERROR;
			}
			*nl = '\0';
			r = files_remove(files,buffer);
			RET_UPDATE(ret,r);
		} 
	r = files_done(files);
	RET_ENDUPDATE(ret,r);
	return ret;
}

static retvalue action_md5sums(int argc,const char *argv[]) {
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
				if( force <= 0 )
					return RET_ERROR;
			}
		}
		return ret;
	} else {
		r = files_initialize(&files,dbdir,mirrordir);
		if( RET_WAS_ERROR(r) )
			return r;
		ret = files_printmd5sums(files);
		r = files_done(files);
		RET_ENDUPDATE(ret,r);
		return ret;
	}
}

static retvalue printout(void *data UNUSED,const char *package,const char *chunk){
	printf("'%s' -> '%s'\n",package,chunk);
	return RET_OK;
}

static retvalue action_dumpcontents(int argc,const char *argv[]) {
	retvalue result,r;
	packagesdb packages;

	if( argc != 2 ) {
		fprintf(stderr,"reprepro _dumpcontents <identifier>\n");
		return RET_ERROR;
	}

	result = packages_initialize(&packages,dbdir,argv[1]);
	if( RET_WAS_ERROR(result) )
		return result;

	result = packages_foreach(packages,printout,NULL,force);

	r = packages_done(packages);
	RET_ENDUPDATE(result,r);
	
	return result;
}

static retvalue export(void *dummy UNUSED,struct distribution *distribution) {

	if( verbose > 0 ) {
		fprintf(stderr,"Exporting %s...\n",distribution->codename);
	}

	return distribution_export(distribution,dbdir,distdir,force,FALSE);
}

static retvalue action_export(int argc,const char *argv[]) {
	retvalue result;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro export [<distributions>]\n");
		return RET_ERROR;
	}
	
	result = distribution_foreach(confdir,argc-1,argv+1,export,NULL,force);
	return result;
}

/***********************update********************************/

static retvalue action_update(int argc,const char *argv[]) {
	retvalue result,r;
	bool_t doexport;
	references refs;
	struct update_pattern *patterns;
	struct distribution *distributions;
	filesdb files;
	struct strlist dereferencedfilekeys;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro update [<distributions>]\n");
		return RET_ERROR;
	}

	result = dirs_make_recursive(listdir);	
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = strlist_init(&dereferencedfilekeys);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	if( RET_WAS_ERROR(result) )
		return result;
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Nothing to do found!\n");
		return RET_NOTHING;
	}

	result = updates_getpatterns(confdir,&patterns,0);
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_calcindices(listdir,patterns,distributions);
	if( RET_WAS_ERROR(result) )
		return result;

	if( !keepunneededlists ) {
		result = updates_clearlists(listdir,distributions);
		if( RET_WAS_ERROR(result) )
			return result;
	}

	r = references_initialize(&refs,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	result = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(result) ) {
		(void)references_done(refs);
		return result;
	}

	result = updates_update(dbdir,methoddir,files,refs,distributions,force,nolistsdownload,keepunreferenced?NULL:&dereferencedfilekeys);

	doexport = force>0 || RET_IS_OK(result);
	if( doexport && verbose >= 0 )
		fprintf(stderr,"Exporting indices...\n");
	while( distributions ) {
		struct distribution *d = distributions->next;

		if( doexport ) {
			r = distribution_export(distributions,dbdir,distdir,force,TRUE);
			RET_ENDUPDATE(result,r);
		}
		
		(void)distribution_free(distributions);
		distributions = d;
	}
	result = possiblyremoveunreferencedfilekeys(result,refs,files,&dereferencedfilekeys);

	r = references_done(refs);
	RET_ENDUPDATE(result,r);
	r = files_done(files);
	RET_ENDUPDATE(result,r);

	return result;
}

static retvalue action_checkupdate(int argc,const char *argv[]) {
	retvalue result;
	struct update_pattern *patterns;
	struct distribution *distributions;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro checkupdate [<distributions>]\n");
		return RET_ERROR;
	}

	result = dirs_make_recursive(listdir);	
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	if( RET_WAS_ERROR(result) )
		return result;
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Nothing to do found!\n");
		return RET_NOTHING;
	}

	result = updates_getpatterns(confdir,&patterns,0);
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_calcindices(listdir,patterns,distributions);
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_checkupdate(dbdir,methoddir,distributions,force,nolistsdownload);

	while( distributions ) {
		struct distribution *d = distributions->next;

		(void)distribution_free(distributions);
		distributions = d;
	}

	return result;
}


/***********************rereferencing*************************/
struct data_binsrcreref { const struct distribution *distribution; references refs;};

static retvalue reref(void *data,struct target *target) {
	retvalue result,r;
	struct data_binsrcreref *d = data;

	result = target_initpackagesdb(target,dbdir);
	if( !RET_WAS_ERROR(result) ) {
		result = target_rereference(target,d->refs,force);
		r = target_closepackagesdb(target);
		RET_ENDUPDATE(result,r);
	}
	return result;
}


static retvalue rereference_dist(void *data,struct distribution *distribution) {
	struct data_binsrcreref dat;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Referencing %s...\n",distribution->codename);
	}

	dat.distribution = distribution;
	dat.refs = data;

	result = distribution_foreach_part(distribution,NULL,NULL,NULL,reref,&dat,force);

	return result;
}

static retvalue action_rereference(int argc,const char *argv[]) {
	retvalue result,r;
	references refs;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro rereference [<distributions>]\n");
		return RET_ERROR;
	}

	r = references_initialize(&refs,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	
	result = distribution_foreach(confdir,argc-1,argv+1,rereference_dist,refs,force);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);

	return result;
}
/***********************checking*************************/
struct data_check { const struct distribution *distribution; references refs; filesdb files;};

static retvalue check_target(void *data,struct target *target) {
	struct data_check *d = data;
	retvalue result,r;

	r = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	result = target_check(target,d->files,d->refs,force);
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	return result;
}

static retvalue check_dist(void *data,struct distribution *distribution) {
	struct data_check *dat=data;
	retvalue result;

	if( verbose > 0 ) {
		fprintf(stderr,"Checking %s...\n",distribution->codename);
	}


	dat->distribution = distribution;

	result = distribution_foreach_part(distribution,component,architecture,packagetype,check_target,dat,force);
	
	return result;
}

static retvalue action_check(int argc,const char *argv[]) {
	retvalue result,r;
	struct data_check dat;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro check [<distributions>]\n");
		return RET_ERROR;
	}

	r = references_initialize(&dat.refs,dbdir);

	if( RET_WAS_ERROR(r) )
		return r;

	r = files_initialize(&dat.files,dbdir,mirrordir);

	if( RET_WAS_ERROR(r) ) {
		(void)references_done(dat.refs);
		return r;
	}
	
	result = distribution_foreach(confdir,argc-1,argv+1,check_dist,&dat,force);
	r = files_done(dat.files);
	RET_ENDUPDATE(result,r);
	r = references_done(dat.refs);
	RET_ENDUPDATE(result,r);

	return result;
}

static retvalue action_checkpool(int argc,const char *argv[]) {
	retvalue result,r;
	filesdb files;

	if( argc < 1 || argc > 2 || (argc == 2 && strcmp(argv[1],"fast") != 0)) {
		fprintf(stderr,"reprepro checkpool [fast] \n");
		return RET_ERROR;
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	result = files_checkpool(files,argc == 2);
	
	r = files_done(files);
	RET_ENDUPDATE(result,r);

	return result;
}

/***********************include******************************************/

static retvalue action_includedeb(int argc,const char *argv[]) {
	retvalue result,r;
	filesdb files;references refs;
	struct distribution *distribution;
	struct overrideinfo *override;
	struct strlist dereferencedfilekeys;
	const char *binarytype;

	if( argc < 3 ) {
		fprintf(stderr,"reprepro [--delete] include[u]deb <distribution> <package>\n");
		return RET_ERROR;
	}
	if( onlyacceptsigned ) {
		fprintf(stderr,"include[u]deb together with --onlyacceptsigned is not yet possible,\n as .[u]deb files cannot be signed yet.\n");
		return RET_ERROR;
	}
	if( strcmp(argv[0],"includeudeb") == 0 ) {
		binarytype="udeb";
		if( packagetype != NULL && strcmp(packagetype,"udeb") != 0 ) {
			fprintf(stderr,"Calling includeudeb with a -T different from 'udeb' makes no sense!\n");
			return RET_ERROR;
		}
	} else if( strcmp(argv[0],"includedeb") == 0 ) {
		binarytype="deb";
		if( packagetype != NULL && strcmp(packagetype,"deb") != 0 ) {
			fprintf(stderr,"Calling includedeb with -T something where something is not 'deb' makes no sense!\n");
			return RET_ERROR;
		}
	} else {
		fprintf(stderr,"Internal error with command parsing!\n");
		return RET_ERROR;
	}

	result = strlist_init(&dereferencedfilekeys);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Could not find '%s' in '%s/distributions'!\n",argv[1],confdir);
		return RET_ERROR;
	}

	// TODO: same for component? (depending on type?)
	if( architecture != NULL && !strlist_in(&distribution->architectures,architecture) ){
		fprintf(stderr,"Cannot force into the architecture '%s' not available in '%s'!\n",architecture,distribution->codename);
		return RET_ERROR;
	}

	override = NULL;
	if( distribution->override != NULL ) {
		result = override_read(overridedir,distribution->override,&override);
		if( RET_WAS_ERROR(result) ) {
			r = distribution_free(distribution);
			RET_ENDUPDATE(result,r);
			return result;
		}
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) ) {
		(void)distribution_free(distribution);
		return r;
	}
	r = references_initialize(&refs,dbdir);
	if( RET_WAS_ERROR(r) ) {
		(void)files_done(files);
		(void)distribution_free(distribution);
		return r;
	}

	result = deb_add(dbdir,refs,files,component,architecture,
			section,priority,binarytype,distribution,argv[2],
			NULL,NULL,override,force,delete,
			keepunreferenced?NULL:&dereferencedfilekeys);

	override_free(override);

	r = distribution_export(distribution,dbdir,distdir,force,TRUE);
	RET_ENDUPDATE(result,r);

	result = possiblyremoveunreferencedfilekeys(result,refs,files,&dereferencedfilekeys);

	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	r = files_done(files);
	RET_ENDUPDATE(result,r);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);

	return result;
}


static retvalue action_includedsc(int argc,const char *argv[]) {
	retvalue result,r;
	filesdb files; references refs;
	struct distribution *distribution;
	struct overrideinfo *srcoverride;
	struct strlist dereferencedfilekeys;

	if( argc < 3 ) {
		fprintf(stderr,"reprepro [--delete] includedsc <distribution> <package>\n");
		return RET_ERROR;
	}

	if( architecture != NULL && strcmp(architecture,"source") != 0 ) {
		fprintf(stderr,"Cannot put a source-package anywhere else than in architecture 'source'!\n");
		return RET_ERROR;
	}
	if( packagetype != NULL && strcmp(packagetype,"dsc") != 0 ) {
		fprintf(stderr,"Cannot put a source-package anywhere else than in type 'dsc'!\n");
		return RET_ERROR;
	}

	result = strlist_init(&dereferencedfilekeys);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	if( RET_WAS_ERROR(result) )
		return result;
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Could not find '%s' in '%s/distributions'!\n",argv[1],confdir);
		return RET_ERROR;
	}
	srcoverride = NULL;
	if( distribution->srcoverride != NULL ) {
		result = override_read(overridedir,distribution->srcoverride,&srcoverride);
		if( RET_WAS_ERROR(result) ) {
			r = distribution_free(distribution);
			RET_ENDUPDATE(result,r);
			return result;
		}
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( !files )
		return RET_ERROR;
	r = references_initialize(&refs,dbdir);
	if( RET_WAS_ERROR(r) ) {
		(void)files_done(files);
		return r;
	}

	result = dsc_add(dbdir,refs,files,component,section,priority,distribution,argv[2],NULL,NULL,NULL,NULL,srcoverride,force,delete,keepunreferenced?NULL:&dereferencedfilekeys,onlyacceptsigned);
	
	override_free(srcoverride);
	r = distribution_export(distribution,dbdir,distdir,force,TRUE);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);

	result = possiblyremoveunreferencedfilekeys(result,refs,files,&dereferencedfilekeys);
	
	r = files_done(files);
	RET_ENDUPDATE(result,r);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);

	return result;
}

static retvalue action_include(int argc,const char *argv[]) {
	retvalue result,r;
	filesdb files;references refs;
	struct distribution *distribution;
	struct overrideinfo *override,*srcoverride;
	struct strlist dereferencedfilekeys;

	if( argc < 3 ) {
		fprintf(stderr,"reprepro [--delete] include <distribution> <.changes-file>\n");
		return RET_ERROR;
	}
	result = strlist_init(&dereferencedfilekeys);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	if( RET_WAS_ERROR(result) )
		return result;
	if( result == RET_NOTHING ) {
		fprintf(stderr,"Could not find '%s' in '%s/distributions'!\n",argv[1],confdir);
		return RET_ERROR;
	}
	override = NULL;
	if( distribution->override != NULL ) {
		result = override_read(overridedir,distribution->override,&override);
		if( RET_WAS_ERROR(result) ) {
			r = distribution_free(distribution);
			RET_ENDUPDATE(result,r);
			return result;
		}
	}
	srcoverride = NULL;
	if( distribution->srcoverride != NULL ) {
		result = override_read(overridedir,distribution->srcoverride,&srcoverride);
		if( RET_WAS_ERROR(result) ) {
			r = distribution_free(distribution);
			RET_ENDUPDATE(result,r);
			return result;
		}
	}

	r = files_initialize(&files,dbdir,mirrordir);
	if( RET_WAS_ERROR(r) )
		return r;
	r = references_initialize(&refs,dbdir);
	if( RET_WAS_ERROR(r) ) {
		(void)files_done(files);
		return r;
	}

	result = changes_add(dbdir,refs,files,packagetype,component,architecture,section,priority,distribution,srcoverride,override,argv[2],force,delete,keepunreferenced?NULL:&dereferencedfilekeys,onlyacceptsigned);

	override_free(override);override_free(srcoverride);
	
	r = distribution_export(distribution,dbdir,distdir,force,TRUE);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);

	result = possiblyremoveunreferencedfilekeys(result,refs,files,&dereferencedfilekeys);
	
	r = files_done(files);
	RET_ENDUPDATE(result,r);
	r = references_done(refs);
	RET_ENDUPDATE(result,r);

	return result;
}

retvalue acquirelock(const char *dbdir) {
	char *lockfile;
	int fd;
	retvalue r;

	// TODO: create directory 
	r = dirs_make_recursive(dbdir);
	if( RET_WAS_ERROR(r) )
		return r;

	lockfile = calc_dirconcat(dbdir,"lockfile");
	if( lockfile == NULL )
		return RET_ERROR_OOM;
	fd = open(lockfile,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NOCTTY,S_IRUSR|S_IWUSR);
	if( fd < 0 ) {
		int e = errno;
		fprintf(stderr,"Error creating lockfile '%s': %d=%m!\n",lockfile,e);
		free(lockfile);
		if( e == EEXIST ) {
			fprintf(stderr,
"The lockfile already exists, there might be another instance with the\n"
"same database dir running. To avoid locking overhead, only one process\n"
"can access the database at the same time. Only delete the lockfile if\n"
"you are sure no other version is still running!\n");

		}
		return RET_ERRNO(e);
	}
	// TODO: do some more locking of this file to avoid problems
	// with the non-atomity of O_EXCL with nfs-filesystems...
	if( close(fd) != 0 ) {
		int e = errno;
		fprintf(stderr,"Error creating lockfile '%s': %d=%m!\n",lockfile,e);
		unlink(lockfile);
		free(lockfile);
		return RET_ERRNO(e);
	
	}
	free(lockfile);
	return RET_OK;
}

void releaselock(const char *dbdir) {
	char *lockfile;

	lockfile = calc_dirconcat(dbdir,"lockfile");
	if( lockfile == NULL )
		return;
	if( unlink(lockfile) != 0 ) {
		int e = errno;
		fprintf(stderr,"Error deleting lockfile '%s': %d=%m!\n",lockfile,e);
		unlink(lockfile);
	}
	free(lockfile);
}

/*********************/
/* argument handling */
/*********************/

static struct action {
	char *name;
	retvalue (*start)(int argc,const char *argv[]);
} actions[] = {
	{"__d", 		action_printargs},
	{"__extractcontrol",	action_extractcontrol},
	{"_detect", 		action_detect},
	{"_forget", 		action_forget},
	{"_md5sums", 		action_md5sums},
	{"_dumpcontents", 	action_dumpcontents},
	{"_removereferences", 	action_removereferences},
	{"_addmd5sums",		action_addmd5sums},
	{"_addreference", 	action_addreference},
        {"remove", 		action_remove},
	{"list", 		action_list},
	{"listfilter", 		action_listfilter},
	{"export", 		action_export},
	{"check", 		action_check},
	{"checkpool", 		action_checkpool},
	{"rereference", 	action_rereference},
	{"dumpreferences", 	action_dumpreferences},
	{"dumpunreferenced", 	action_dumpunreferenced},
	{"deleteunreferenced", 	action_deleteunreferenced},
	{"update",		action_update},
	{"checkupdate",		action_checkupdate},
	{"includedeb",		action_includedeb},
	{"includeudeb",		action_includedeb},
	{"includedsc",		action_includedsc},
	{"include",		action_include},
	{NULL,NULL}
};

#define LO_DELETE 1
#define LO_KEEPUNREFERENCED 2
#define LO_KEEPUNNEEDEDLISTS 3
#define LO_NOHTINGISERROR 4
#define LO_NOLISTDOWNLOAD 5
#define LO_ONLYACCEPTSIGNED 6
#define LO_DISTDIR 10
#define LO_DBDIR 11
#define LO_LISTDIR 12
#define LO_OVERRIDEDIR 13
#define LO_CONFDIR 14
#define LO_METHODDIR 15
#define LO_VERSION 20
int longoption = 0;

int main(int argc,char *argv[]) {
	static struct option longopts[] = {
		{"delete", 0, &longoption,LO_DELETE},
		{"basedir", 1, NULL, 'b'},
		{"ignore", 1, NULL, 'i'},
		{"methoddir", 1, &longoption, LO_METHODDIR},
		{"distdir", 1, &longoption, LO_DISTDIR},
		{"dbdir", 1, &longoption, LO_DBDIR},
		{"listdir", 1, &longoption, LO_LISTDIR},
		{"overridedir", 1, &longoption, LO_OVERRIDEDIR},
		{"confdir", 1, &longoption, LO_CONFDIR},
		{"section", 1, NULL, 'S'},
		{"priority", 1, NULL, 'P'},
		{"component", 1, NULL, 'C'},
		{"architecture", 1, NULL, 'A'},
		{"type", 1, NULL, 'T'},
		{"help", 0, NULL, 'h'},
		{"verbose", 0, NULL, 'v'},
		{"version", 0, &longoption, LO_VERSION},
		{"nothingiserror", 0, &longoption, LO_NOHTINGISERROR},
		{"nolistsdownload", 0, &longoption, LO_NOLISTDOWNLOAD},
		{"keepunreferencedfiles", 0, &longoption, LO_KEEPUNREFERENCED},
		{"keepunneededlists", 0, &longoption, LO_KEEPUNNEEDEDLISTS},
		{"onlyacceptsigned", 0, &longoption, LO_ONLYACCEPTSIGNED},
		{"force", 0, NULL, 'f'},
		{NULL, 0, NULL, 0}
	};
	int c;struct action *a;
	retvalue r;

	init_ignores();


	while( (c = getopt_long(argc,argv,"+fVvhb:P:i:A:C:S:T:",longopts,NULL)) != -1 ) {
		switch( c ) {
			case 'h':
				printf(
"reprepro - Produce and Manage and Debian package repository\n\n"
"options:\n"
" -h, --help:                        Show this help\n"
" -i  --ignore <flag>:               Ignore errors of type <flag>.\n"
"     --keepunreferencedfiles:       Do not delete files no longer needed.\n"
"     --delete:                      Delete included files if reasonable.\n"
" -b, --basedir <dir>:               Base-dir (will overwrite prior given\n"
"                                                        -d, -D, -L, -c).\n"
"     --distdir <dir>:               Directory to place the \"dists\" dir in.\n"
"     --dbdir <dir>:                 Directory to place the database in.\n"
"     --listdir <dir>:               Directory to place downloaded lists in.\n"
"     --confdir <dir>:               Directory to search configuration in.\n"
"     --overridedir <dir>:           Directory to search override files in.\n"
"     --methodir <dir>:              Use instead of /usr/lib/apt/methods/\n"
" -S, --section <section>:           Force include* to set section.\n"
" -P, --priority <priority>:         Force include* to set priority.\n"
" -C, --component <component>: 	     Add,list or delete only in component.\n"
" -A, --architecture <architecture>: Add,list or delete only to architecture.\n"
" -T, --type <type>:                 Add,list or delete only type (dsc,deb,udeb).\n"
"\n"
"actions (selection, for more see manpage):\n"
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
" includeudeb <distribution> <.udeb-file>\n"
"       Inlcude the given installer binary package.\n"
" includedsc <distribution> <.dsc-file>\n"
"       Inlcude the given source package.\n"
" list <distribution> <package-name>\n"
"       List all packages by the given name occuring in the given distribution.\n"
" listfilter <distribution> <condition>\n"
"       List all packages in the given distribution matching the condition.\n"
"\n"
						);
				exit(EXIT_SUCCESS);
			case '\0':
				switch( longoption ) {
					case LO_DELETE:
						delete++;
						break;
					case LO_ONLYACCEPTSIGNED:
						onlyacceptsigned = TRUE;
						break;
					case LO_KEEPUNREFERENCED:
						keepunreferenced=TRUE;
						break;
					case LO_KEEPUNNEEDEDLISTS:
						keepunneededlists=TRUE;
						break;
					case LO_NOHTINGISERROR:
						nothingiserror=TRUE;
						break;
					case LO_NOLISTDOWNLOAD:
						nolistsdownload=TRUE;
						break;
					case LO_DISTDIR:
						distdir = strdup(optarg);
						break;
					case LO_DBDIR:
						dbdir = strdup(optarg);
						break;
					case LO_LISTDIR:
						listdir = strdup(optarg);
						break;
					case LO_OVERRIDEDIR:
						overridedir = strdup(optarg);
						break;
					case LO_CONFDIR:
						confdir = strdup(optarg);
						break;
					case LO_METHODDIR:
						methoddir = strdup(optarg);
						break;
					case LO_VERSION:
						fprintf(stderr,"%s: This is " PACKAGE " version " VERSION "\n",argv[0]);
						exit(EXIT_SUCCESS);
					default:
						fprintf (stderr,"Error parsing arguments!\n");
						exit(EXIT_FAILURE);
				}
				longoption = 0;
				break;
			case 'v':
				verbose++;
				break;
			case 'V':
				verbose+=5;
				break;
			case 'f':
				force++;
				break;
			case 'b':
				mirrordir=strdup(optarg);
				distdir=calc_dirconcat(optarg,"dists");
				dbdir=calc_dirconcat(optarg,"db");
				listdir=calc_dirconcat(optarg,"lists");
				confdir=calc_dirconcat(optarg,"conf");
				overridedir=calc_dirconcat(optarg,"override");
				break;
			case 'i':
				r = add_ignore(optarg);
				if( RET_WAS_ERROR(r) ) {
					exit(EXIT_FAILURE);
				}
				break;
			case 'C':
				component = strdup(optarg);
				break;
			case 'A':
				architecture = strdup(optarg);
				break;
			case 'T':
				packagetype = strdup(optarg);
				break;
			case 'S':
				section = strdup(optarg);
				break;
			case 'P':
				priority = strdup(optarg);
				break;
			case '?':
				/* getopt_long should have already given an error msg */
				exit(EXIT_FAILURE);
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
			retvalue r;
			r = acquirelock(dbdir);
			if( RET_IS_OK(r) )  {
				 r = a->start(argc-optind,(const char**)argv+optind);
				releaselock(dbdir);
			}
			return EXIT_RET(r);
		} else
			a++;
	}

	fprintf(stderr,"Unknown action '%s'. (see --help for available options and actions)\n",argv[optind]);
	return EXIT_FAILURE;
}

