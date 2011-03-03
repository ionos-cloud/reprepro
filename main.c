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
#include <strings.h>
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
#ifndef STD_METHOD_DIR
#define STD_METHOD_DIR "/usr/lib/apt/methods"
#endif

/* global options */
static char /*@only@*/ /*@notnull@*/ // *g*
	*mirrordir = NULL ,
	*distdir = NULL,
	*dbdir = NULL,
	*listdir = NULL,
	*confdir = NULL,
	*overridedir = NULL,
	*methoddir = NULL;
static char /*@only@*/ /*@null@*/ 
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

#define ACTION_N(name) static retvalue action_n_ ## name ( \
			UNUSED(references dummy1), 	\
			UNUSED(filesdb dummy2), 	\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_U_R(name) static retvalue action_r_ ## name ( \
			references references, 		\
			UNUSED(filesdb dummy2), 	\
			UNUSED(struct strlist* dummy3), \
			int argc,UNUSED(const char *dummy4[]))

#define ACTION_R(name) static retvalue action_r_ ## name ( \
			references references, 		\
			UNUSED(filesdb dummy2), 	\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_U_F(name) static retvalue action_f_ ## name ( \
			UNUSED(references dummy1), 	\
			filesdb filesdb,		\
			UNUSED(struct strlist* dummy3), \
			int argc,UNUSED(const char *dummy4[]))

#define ACTION_F(name) static retvalue action_f_ ## name ( \
			UNUSED(references dummy1), 	\
			filesdb filesdb,		\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_RF(name) static retvalue action_rf_ ## name ( \
			references references, 		\
			filesdb filesdb,		\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_U_RF(name) static retvalue action_rf_ ## name ( \
			references references, 		\
			filesdb filesdb,		\
			UNUSED(struct strlist* dummy3), \
			int argc,UNUSED(const char *dumym4[]))

#define ACTION_D(name) static retvalue action_d_ ## name ( \
			references references, 		\
			filesdb filesdb,		\
			struct strlist* dereferenced, 	\
			int argc,const char *argv[])

#define ACTION_D_U(name) static retvalue action_d_ ## name ( \
			references references, 		\
			UNUSED(filesdb dummy2),		\
			struct strlist* dereferenced, 	\
			int argc,const char *argv[])

ACTION_N(printargs) {
	int i;

	fprintf(stderr,"argc: %d\n",argc);
	for( i=0 ; i < argc ; i++ ) {
		fprintf(stderr,"%s\n",argv[i]);
	}
	return 0;
}
ACTION_N(extractcontrol) {
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


ACTION_U_F(addmd5sums) {
	char buffer[2000],*c,*m;
	retvalue result,r;

	if( argc != 1 ) {
		fprintf(stderr,"reprepro _addmd5sums < <data>\n");
		return RET_ERROR;
	}

	result = RET_NOTHING;
	
	while( fgets(buffer,1999,stdin) != NULL ) {
		c = strchr(buffer,'\n');
		if( ! c ) {
			fprintf(stderr,"Line too long\n");
			return RET_ERROR;
		}
		*c = '\0';
		m = strchr(buffer,' ');
		if( ! m ) {
			fprintf(stderr,"Malformed line\n");
			return RET_ERROR;
		}
		*m = '\0'; m++;
		r = files_add(filesdb,buffer,m);
		RET_UPDATE(result,r);

	}
	return result;
}


ACTION_R(removereferences) {

	if( argc != 2 ) {
		fprintf(stderr,"reprepro _removereferences <identifier>\n");
		return RET_ERROR;
	}
	return references_remove(references,argv[1]);
}


ACTION_U_R(dumpreferences) {

	if( argc != 1 ) {
		fprintf(stderr,"reprepro dumpreferences\n");
		return RET_ERROR;
	}
	return references_dump(references);
}

struct fileref { /*@temp@*/references refs; };

static retvalue checkifreferenced(void *data,const char *filekey,UNUSED(const char *md5sum)) {
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

ACTION_U_RF(dumpunreferenced) {
	retvalue result;
	struct fileref dist;

	if( argc != 1 ) {
		fprintf(stderr,"reprepro dumpunreferenced\n");
		return RET_ERROR;
	}
	dist.refs = references;
	result = files_foreach(filesdb,checkifreferenced,&dist);
	dist.refs = NULL;
	return result;
}

struct filedelref { /*@temp@*/references references; /*@temp@*/filesdb filesdb; };

static retvalue deleteifunreferenced(void *data,const char *filekey,UNUSED(const char *md5sum)) {
	struct filedelref *dist = data;
	retvalue r;

	r = references_isused(dist->references,filekey);
	if( r == RET_NOTHING ) {
		r = files_deleteandremove(dist->filesdb,filekey);
		return r;
	} else if( RET_IS_OK(r) ) {
		return RET_NOTHING;
	} else
		return r;
}

ACTION_U_RF(deleteunreferenced) {
	retvalue result;
	struct filedelref dist;

	if( argc != 1 ) {
		fprintf(stderr,"reprepro deleteunreferenced\n");
		return RET_ERROR;
	}
	if( keepunreferenced ) {
		fprintf(stderr,"Calling deleteunreferenced with --keepunreferencedfiles does not really make sense, does it?\n");
		return RET_ERROR;
	}
	dist.references = references;
	dist.filesdb = filesdb;
	result = files_foreach(filesdb,deleteifunreferenced,&dist);
	dist.references = NULL;
	dist.filesdb = NULL;
	return result;
}

ACTION_R(addreference) {

	if( argc != 3 ) {
		fprintf(stderr,"reprepro _addreference <reference> <referee>\n");
		return RET_ERROR;
	}
	return references_increment(references,argv[1],argv[2]);
}


struct remove_args {/*@temp@*/references refs; int count; /*@temp@*/ const char * const *names; bool_t *gotremoved; int todo;/*@temp@*/struct strlist *removedfiles;};

static retvalue remove_from_target(/*@temp@*/void *data, struct target *target) {
	retvalue result,r;
	int i;
	struct remove_args *d = data;

	result = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = RET_NOTHING;
	for( i = 0 ; i < d->count ; i++ ){
		r = target_removepackage(target,d->refs,d->names[i],d->removedfiles);
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

ACTION_D_U(remove) {
	retvalue result,r;
	struct distribution *distribution;
	struct remove_args d;

	if( argc < 3  ) {
		fprintf(stderr,"reprepro [-C <component>] [-A <architecture>] [-T <type>] remove <codename> <package-names>\n");
		return RET_ERROR;
	}
	r = distribution_get(&distribution,confdir,argv[1]);
	assert( r != RET_NOTHING);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	d.count = argc-2;
	d.names = argv+2;
	d.todo = d.count;
	d.gotremoved = calloc(d.count,sizeof(*d.gotremoved));
	d.refs = references;
	d.removedfiles = dereferenced;
	if( d.gotremoved == NULL )
		result = RET_ERROR_OOM;
	else
		result = distribution_foreach_part(distribution,component,architecture,packagetype,remove_from_target,&d,force);
	d.refs = NULL;
	d.removedfiles = NULL;

	if( d.todo < d.count ) {
		r = distribution_export(distribution,confdir,dbdir,distdir,force,TRUE);
		RET_ENDUPDATE(result,r);
	}
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	if( verbose >= 0 && !RET_WAS_ERROR(result) && d.todo > 0 ) {
		(void)fputs("Not removed as not found: ",stderr);
		while( d.count > 0 ) {
			d.count--;
			assert(d.gotremoved != NULL);
			if( ! d.gotremoved[d.count] ) {
				(void)fputs(d.names[d.count],stderr);
				d.todo--;
				if( d.todo > 0 )
					(void)fputs(", ",stderr);
			}
		}
		(void)fputc('\n',stderr);
	}
	free(d.gotremoved);
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

ACTION_N(list) {
	retvalue r,result;
	struct distribution *distribution;

	if( argc != 3  ) {
		fprintf(stderr,"reprepro [-C <component>] [-A <architecture>] [-T <type>] list <codename> <package-name>\n");
		return RET_ERROR;
	}
	r = distribution_get(&distribution,confdir,argv[1]);
	assert( r != RET_NOTHING);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	result = distribution_foreach_part(distribution,component,architecture,packagetype,list_in_target,(void*)argv[2],force);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	return result;
}


struct listfilter { /*@temp@*/ struct target *target; /*@temp@*/ term *condition; };

static retvalue listfilterprint(/*@temp@*/void *data,const char *packagename,const char *control){
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

static retvalue listfilter_in_target(/*@temp@*/void *data, /*@temp@*/struct target *target) {
	retvalue r,result;
	/*@temp@*/ struct listfilter d;

	result = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	d.target = target;
	d.condition = data;
	result = packages_foreach(target->packages,listfilterprint,&d,force);
	d.target = NULL;
	d.condition = NULL;

	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	return result;
}

ACTION_N(listfilter) {
	retvalue r,result;
	struct distribution *distribution;
	term *condition;

	if( argc != 3  ) {
		fprintf(stderr,"reprepro [-C <component>] [-A <architecture>] [-T <type>] listfilter <codename> <term to describe which packages to list>\n");
		return RET_ERROR;
	}
	r = distribution_get(&distribution,confdir,argv[1]);
	assert( r != RET_NOTHING);
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

ACTION_F(detect) {
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	ret = RET_NOTHING;
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			r = files_detect(filesdb,argv[i]);
			RET_UPDATE(ret,r);
		}

	} else
		while( fgets(buffer,4999,stdin) ) {
			nl = strchr(buffer,'\n');
			if( !nl ) {
				return RET_ERROR;
			}
			*nl = '\0';
			r = files_detect(filesdb,buffer);
			RET_UPDATE(ret,r);
		} 
	return ret;
}

ACTION_F(forget) {
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	ret = RET_NOTHING;
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			r = files_remove(filesdb,argv[i]);
			RET_UPDATE(ret,r);
		}

	} else
		while( fgets(buffer,4999,stdin) ) {
			nl = strchr(buffer,'\n');
			if( !nl ) {
				return RET_ERROR;
			}
			*nl = '\0';
			r = files_remove(filesdb,buffer);
			RET_UPDATE(ret,r);
		} 
	return ret;
}

ACTION_U_F(listmd5sums) {
	if( argc != 1 ) {
		fprintf(stderr,"reprepro _listmd5sums \n");
		return RET_ERROR;
	}
	return files_printmd5sums(filesdb);
}

static retvalue printout(UNUSED(void *data),const char *package,const char *chunk){
	printf("'%s' -> '%s'\n",package,chunk);
	return RET_OK;
}

ACTION_N(dumpcontents) {
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

ACTION_N(export) {
	retvalue result,r;
	struct distribution *distributions,*d;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro export [<distributions>]\n");
		return RET_ERROR;
	}
	
	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING);
	if( RET_WAS_ERROR(result) )
		return result;
	result = RET_NOTHING;
	for( d = distributions ; d != NULL ; d = d->next ) {
		if( verbose > 0 ) {
			fprintf(stderr,"Exporting %s...\n",d->codename);
		}

		r = distribution_export(d,confdir,dbdir,distdir,force,FALSE);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force<= 0 )
			return r;
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);
	return result;
}

/***********************update********************************/

ACTION_D(update) {
	retvalue result,r;
	bool_t doexport;
	struct update_pattern *patterns;
	struct distribution *distributions;
	struct update_distribution *u_distributions;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro update [<distributions>]\n");
		return RET_ERROR;
	}

	result = dirs_make_recursive(listdir);	
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_getpatterns(confdir,&patterns);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}

	result = updates_calcindices(listdir,patterns,distributions,&u_distributions);
	if( RET_WAS_ERROR(result) ) {
		updates_freepatterns(patterns);
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}

	if( !keepunneededlists ) {
		result = updates_clearlists(listdir,u_distributions);
	}
	if( !RET_WAS_ERROR(result) )
		result = updates_update(dbdir,methoddir,filesdb,references,u_distributions,force,nolistsdownload,dereferenced);
	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);

	doexport = force>0 || RET_IS_OK(result);
	if( doexport && verbose >= 0 )
		fprintf(stderr,"Exporting indices...\n");
	if( doexport )
		r = distribution_exportandfreelist(distributions,confdir,dbdir,distdir,force);
	else
		r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_N(checkupdate) {
	retvalue result,r;
	struct update_pattern *patterns;
	struct distribution *distributions;
	struct update_distribution *u_distributions;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro checkupdate [<distributions>]\n");
		return RET_ERROR;
	}

	result = dirs_make_recursive(listdir);	
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING);
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_getpatterns(confdir,&patterns);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}

	result = updates_calcindices(listdir,patterns,distributions,&u_distributions);
	if( RET_WAS_ERROR(result) ) {
		updates_freepatterns(patterns);
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}

	result = updates_checkupdate(dbdir,methoddir,u_distributions,force,nolistsdownload);

	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}


/***********************rereferencing*************************/
struct data_binsrcreref { /*@temp@*/const struct distribution *distribution; /*@temp@*/references refs;};

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


ACTION_R(rereference) {
	retvalue result,r;
	struct distribution *distributions,*d;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro rereference [<distributions>]\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = distributions ; d != NULL ; d = d->next ) {
		struct data_binsrcreref dat;

		if( verbose > 0 ) {
			fprintf(stderr,"Referencing %s...\n",d->codename);
		}
		dat.distribution = d;
		dat.refs = references;

		r = distribution_foreach_part(d,component,architecture,packagetype,
				reref,&dat,force);
		dat.refs = NULL;
		dat.distribution = NULL;
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}
/***********************checking*************************/
struct data_check { /*@temp@*/const struct distribution *distribution; /*@temp@*/references references; /*@temp@*/filesdb filesdb;};

static retvalue check_target(void *data,struct target *target) {
	struct data_check *d = data;
	retvalue result,r;

	r = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	result = target_check(target,d->filesdb,d->references,force);
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	return result;
}

ACTION_RF(check) {
	retvalue result,r;
	struct distribution *distributions,*d;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro check [<distributions>]\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = distributions ; d != NULL ; d = d->next ) {
		struct data_check dat;

		if( verbose > 0 ) {
			fprintf(stderr,"Checking %s...\n",d->codename);
		}

		dat.distribution = d;
		dat.references = references;
		dat.filesdb = filesdb;

		r = distribution_foreach_part(d,component,architecture,packagetype,check_target,&dat,force);
		dat.references = NULL;
		dat.filesdb = NULL;
		dat.distribution = NULL;
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_F(checkpool) {

	if( argc < 1 || argc > 2 || (argc == 2 && strcmp(argv[1],"fast") != 0)) {
		fprintf(stderr,"reprepro checkpool [fast] \n");
		return RET_ERROR;
	}

	return files_checkpool(filesdb,argc == 2);
}

/***********************include******************************************/

ACTION_D(includedeb) {
	retvalue result,r;
	struct distribution *distribution;
	struct overrideinfo *override;
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

	result = distribution_get(&distribution,confdir,argv[1]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	// TODO: same for component? (depending on type?)
	if( architecture != NULL && !strlist_in(&distribution->architectures,architecture) ){
		fprintf(stderr,"Cannot force into the architecture '%s' not available in '%s'!\n",architecture,distribution->codename);
		(void)distribution_free(distribution);
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

	result = deb_add(dbdir,references,filesdb,component,architecture,
			section,priority,binarytype,distribution,argv[2],
			NULL,NULL,override,force,delete,
			dereferenced);

	override_free(override);

	r = distribution_export(distribution,confdir,dbdir,distdir,force,TRUE);
	RET_ENDUPDATE(result,r);

	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);

	return result;
}


ACTION_D(includedsc) {
	retvalue result,r;
	struct distribution *distribution;
	struct overrideinfo *srcoverride;

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

	result = distribution_get(&distribution,confdir,argv[1]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;
	srcoverride = NULL;
	if( distribution->srcoverride != NULL ) {
		result = override_read(overridedir,distribution->srcoverride,&srcoverride);
		if( RET_WAS_ERROR(result) ) {
			r = distribution_free(distribution);
			RET_ENDUPDATE(result,r);
			return result;
		}
	}

	result = dsc_add(dbdir,references,filesdb,component,section,priority,distribution,argv[2],NULL,NULL,NULL,NULL,srcoverride,force,delete,dereferenced,onlyacceptsigned);
	
	override_free(srcoverride);
	r = distribution_export(distribution,confdir,dbdir,distdir,force,TRUE);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_D(include) {
	retvalue result,r;
	struct distribution *distribution;
	struct overrideinfo *override,*srcoverride;

	if( argc < 3 ) {
		fprintf(stderr,"reprepro [--delete] include <distribution> <.changes-file>\n");
		return RET_ERROR;
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;
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

	result = changes_add(dbdir,references,filesdb,packagetype,component,architecture,section,priority,distribution,srcoverride,override,argv[2],force,delete,dereferenced,onlyacceptsigned);

	override_free(override);override_free(srcoverride);
	
	r = distribution_export(distribution,confdir,dbdir,distdir,force,TRUE);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);

	return result;
}

static retvalue acquirelock(const char *dbdir) {
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
		(void)unlink(lockfile);
		free(lockfile);
		return RET_ERRNO(e);
	
	}
	free(lockfile);
	return RET_OK;
}

static void releaselock(const char *dbdir) {
	char *lockfile;

	lockfile = calc_dirconcat(dbdir,"lockfile");
	if( lockfile == NULL )
		return;
	if( unlink(lockfile) != 0 ) {
		int e = errno;
		fprintf(stderr,"Error deleting lockfile '%s': %d=%m!\n",lockfile,e);
		(void)unlink(lockfile);
	}
	free(lockfile);
}

/*********************/
/* argument handling */
/*********************/

#define NEED_REFERENCES 1
#define NEED_FILESDB 2
#define NEED_DEREF 4
#define A_N(w) action_n_ ## w, 0
#define A_F(w) action_f_ ## w, NEED_FILESDB
#define A_R(w) action_r_ ## w, NEED_REFERENCES
#define A_RF(w) action_rf_ ## w, NEED_FILESDB|NEED_REFERENCES
/* to dereference files, one needs files and references database: */
#define A_D(w) action_d_ ## w, NEED_FILESDB|NEED_REFERENCES|NEED_DEREF 

static const struct action {
	char *name;
	retvalue (*start)(
			/*@null@*/references references, 
			/*@null@*/filesdb filesdb,
			/*@null@*/struct strlist *dereferencedfilekeys,
			int argc,const char *argv[]);
	int needs;
} all_actions[] = {
	{"__d", 		A_N(printargs)},
	{"__extractcontrol",	A_N(extractcontrol)},
	{"_detect", 		A_F(detect)},
	{"_forget", 		A_F(forget)},
	{"_listmd5sums",	A_F(listmd5sums)},
	{"_addmd5sums",		A_F(addmd5sums)},
	{"_dumpcontents", 	A_N(dumpcontents)},
	{"_removereferences", 	A_R(removereferences)},
	{"_addreference", 	A_R(addreference)},
	{"remove", 		A_D(remove)},
	{"list", 		A_N(list)},
	{"listfilter", 		A_N(listfilter)},
	{"export", 		A_N(export)},
	{"check", 		A_RF(check)},
	{"checkpool", 		A_F(checkpool)},
	{"rereference", 	A_R(rereference)},
	{"dumpreferences", 	A_R(dumpreferences)},
	{"dumpunreferenced", 	A_RF(dumpunreferenced)},
	{"deleteunreferenced", 	A_RF(deleteunreferenced)},
	{"update",		A_D(update)},
	{"checkupdate",		A_N(checkupdate)},
	{"includedeb",		A_D(includedeb)},
	{"includeudeb",		A_D(includedeb)},
	{"includedsc",		A_D(includedsc)},
	{"include",		A_D(include)},
	{NULL,NULL,0}
};
#undef A_N
#undef A_F
#undef A_R
#undef A_RF
#undef A_F

static retvalue callaction(const struct action *action,int argc,const char *argv[]) {
	retvalue result;
	references references;
	filesdb filesdb;
	struct strlist dereferencedfilekeys;
	bool_t deletederef;

	assert(action != NULL);
	
	deletederef = ISSET(action->needs,NEED_DEREF) && !keepunreferenced;
	
	result = acquirelock(dbdir);
	if( !RET_IS_OK(result) )
		return result;

	if( ISSET(action->needs,NEED_REFERENCES) )
		result = references_initialize(&references,dbdir);
	else
		references = NULL;

	assert( result != RET_NOTHING );
	if( RET_IS_OK(result) ) {

		if( ISSET(action->needs,NEED_FILESDB) )
			result = files_initialize(&filesdb,dbdir,mirrordir);
		else
			filesdb = NULL;

		assert( result != RET_NOTHING );
		if( RET_IS_OK(result) ) {

			if( deletederef ) {
				assert( ISSET(action->needs,NEED_REFERENCES) );
				assert( ISSET(action->needs,NEED_REFERENCES) );
				result = strlist_init(&dereferencedfilekeys);
			}

			assert( result != RET_NOTHING );
			if( RET_IS_OK(result) ) {
				result = action->start(references,filesdb,
					deletederef?&dereferencedfilekeys:NULL,
					argc,argv);

				if( deletederef ) {
					if( dereferencedfilekeys.count > 0 ) {
					    if( RET_IS_OK(result) ) {
						retvalue r;

						assert(filesdb!=NULL);
						assert(references!=NULL);

						if( verbose >= 0 )
					  	    fprintf(stderr,
"Deleting files no longer referenced...\n");
						r = removeunreferencedfiles(
							references,filesdb,
							&dereferencedfilekeys);
						RET_UPDATE(result,r);
					    } else {
						    fprintf(stderr,
"Not deleting possibly left over files due to previous errors.\n"
"(To avoid the files in the still existing index files vanishing)\n"
"Use dumpunreferenced/deleteunreferenced to show/delete files without referenes.\n");
					    }
					}
					strlist_done(&dereferencedfilekeys);
				}
			}
			if( ISSET(action->needs,NEED_FILESDB) ) {
				retvalue r = files_done(filesdb);
				RET_ENDUPDATE(result,r);
			}
		}

		if( ISSET(action->needs,NEED_REFERENCES) ) {
			retvalue r = references_done(references);
			RET_ENDUPDATE(result,r);
		}
	}
	releaselock(dbdir);
	return result;
}


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
static int longoption = 0;

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
	const struct action *a;
	retvalue r;
	int c;

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
						free(distdir);
						distdir = strdup(optarg);
						break;
					case LO_DBDIR:
						free(dbdir);
						dbdir = strdup(optarg);
						break;
					case LO_LISTDIR:
						free(listdir);
						listdir = strdup(optarg);
						break;
					case LO_OVERRIDEDIR:
						free(overridedir);
						overridedir = strdup(optarg);
						break;
					case LO_CONFDIR:
						free(confdir);
						confdir = strdup(optarg);
						break;
					case LO_METHODDIR:
						free(methoddir);
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
				free(mirrordir);
				mirrordir=strdup(optarg);
				break;
			case 'i':
				r = add_ignore(optarg);
				if( RET_WAS_ERROR(r) ) {
					exit(EXIT_FAILURE);
				}
				break;
			case 'C':
				free(component);
				component = strdup(optarg);
				break;
			case 'A':
				free(architecture);
				architecture = strdup(optarg);
				break;
			case 'T':
				free(packagetype);
				packagetype = strdup(optarg);
				break;
			case 'S':
				free(section);
				section = strdup(optarg);
				break;
			case 'P':
				free(priority);
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
	if( mirrordir == NULL ) {
		mirrordir=strdup(STD_BASE_DIR);
		if( mirrordir == NULL ) {
			(void)fputs("Out of Memory!\n",stderr);
			exit(EXIT_FAILURE);
		}
	}
	if( methoddir == NULL )
		methoddir = strdup(STD_METHOD_DIR);
	if( distdir == NULL )
		distdir=calc_dirconcat(mirrordir,"dists");
	if( dbdir == NULL )
		dbdir=calc_dirconcat(mirrordir,"db");
	if( listdir == NULL )
		listdir=calc_dirconcat(mirrordir,"lists");
	if( confdir == NULL )
		confdir=calc_dirconcat(mirrordir,"conf");
	if( overridedir == NULL )
		overridedir=calc_dirconcat(mirrordir,"override");
	if( distdir == NULL || dbdir == NULL || listdir == NULL 
			|| confdir == NULL || overridedir == NULL || methoddir == NULL) {
		(void)fputs("Out of Memory!\n",stderr);
		exit(EXIT_FAILURE);
	}
	a = all_actions;
	while( a->name ) {
		if( strcasecmp(a->name,argv[optind]) == 0 ) {
			r = callaction(a,argc-optind,(const char**)argv+optind);
			/* yeah, freeing all this stuff before exiting is
			 * stupid, but it keeps valgrind logs easier 
			 * readable */
			signatures_done();
			free(dbdir);
			free(distdir);
			free(listdir);
			free(confdir);
			free(overridedir);
			free(mirrordir);
			free(methoddir);
			free(component);
			free(architecture);
			free(packagetype);
			free(section);
			free(priority);
			exit(EXIT_RET(r));
		} else
			a++;
	}

	fprintf(stderr,"Unknown action '%s'. (see --help for available options and actions)\n",argv[optind]);
	signatures_done();
	free(dbdir);
	free(distdir);
	free(listdir);
	free(confdir);
	free(overridedir);
	free(mirrordir);
	free(methoddir);
	free(component);
	free(architecture);
	free(packagetype);
	free(section);
	free(priority);
	exit(EXIT_FAILURE);
}

