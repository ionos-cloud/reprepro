/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <fcntl.h>
#include <signal.h>
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
#include "pull.h"
#include "upgradelist.h"
#include "signature.h"
#include "debfile.h"
#include "checkindeb.h"
#include "checkindsc.h"
#include "checkin.h"
#include "downloadcache.h"
#include "terms.h"
#include "tracking.h"
#include "optionsfile.h"
#include "dpkgversions.h"
#include "incoming.h"
#include "override.h"

#ifndef STD_BASE_DIR
#define STD_BASE_DIR "."
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
static bool_t	nothingiserror = FALSE;
static bool_t	nolistsdownload = FALSE;
static bool_t	keepunreferenced = FALSE;
static bool_t	keepunneededlists = FALSE;
static bool_t	keepdirectories = FALSE;
static bool_t	askforpassphrase = FALSE;
static bool_t	skipold = TRUE;
static enum exportwhen export = EXPORT_NORMAL;
int		verbose = 0;

/* define for each config value an owner, and only higher owners are allowed
 * to change something owned by lower owners. */
enum config_option_owner config_state,
#define O(x) owner_ ## x = CONFIG_OWNER_DEFAULT
O(mirrordir), O(distdir), O(dbdir), O(listdir), O(confdir), O(overridedir), O(methoddir), O(section), O(priority), O(component), O(architecture), O(packagetype), O(nothingiserror), O(nolistsdownload), O(keepunreferenced), O(keepunneededlists), O(keepdirectories), O(askforpassphrase), O(skipold), O(export);
#undef O

#define CONFIGSET(variable,value) if(owner_ ## variable <= config_state) { \
					owner_ ## variable = config_state; \
					variable = value; }
#define CONFIGDUP(variable,value) if(owner_ ## variable <= config_state) { \
					owner_ ## variable = config_state; \
					free(variable); \
					variable = strdup(value); \
					if( variable == NULL ) { \
						fputs("Out of Memory!",stderr); \
						exit(EXIT_FAILURE); \
					} }

static inline retvalue removeunreferencedfiles(references refs,filesdb files,struct strlist *dereferencedfilekeys) {
	int i;
	retvalue result,r;
	result = RET_OK;

	for( i = 0 ; i < dereferencedfilekeys->count ; i++ ) {
		const char *filekey = dereferencedfilekeys->values[i];

		r = references_isused(refs,filekey);
		if( r == RET_NOTHING ) {
			r = files_deleteandremove(files,filekey,!keepdirectories,TRUE);
			if( r == RET_NOTHING ) {
				/* not found, check if it was us removing it */
				int j;
				for( j = i-1 ; j >= 0 ; j-- ) {
					if( strcmp(dereferencedfilekeys->values[i],
					    dereferencedfilekeys->values[j]) == 0 )
						break;
				}
				if( j < 0 ) {
					fprintf(stderr, "To be forgotten filekey '%s' was not known.\n", dereferencedfilekeys->values[i]);
					r = RET_ERROR_MISSING;
				}
			}
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
#define ACTION_D_UU(name) static retvalue action_d_ ## name ( \
			references references, 		\
			UNUSED(filesdb dummy2),		\
			struct strlist* dereferenced, 	\
			int argc,UNUSED(const char *dummy4[]))

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
ACTION_N(extractfilelist) {
	retvalue result;
	char *filelist;

	if( argc != 2 ) {
		fprintf(stderr,"reprepro __extractfilelist <.deb-file>\n");
		return RET_ERROR;
	}

	result = getfilelist(&filelist,argv[1]);

	if( RET_IS_OK(result) ) {
		const char *p = filelist;
		while( *p != '\0' ) {
			puts(p);
			p += strlen(p)+1;
		}
		free(filelist);
	}
	return result;
}

ACTION_F(fakeemptyfilelist) {
	if( argc != 2 ) {
		fprintf(stderr,"reprepro _fakeemptyfilelist <filekey>\n");
		return RET_ERROR;
	}
	return files_addfilelist(filesdb, argv[1], "");
}


ACTION_F(generatefilelists) {

	if( argc < 1 || argc > 2 || (argc == 2 && strcmp(argv[1],"reread") != 0) ) {
		fprintf(stderr,"reprepro generatefilelists [reread]\n");
		return RET_ERROR;
	}

	return files_regenerate_filelist(filesdb, argc == 2);
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
		if( c == NULL ) {
			fprintf(stderr,"Line too long\n");
			return RET_ERROR;
		}
		*c = '\0';
		m = strchr(buffer,' ');
		if( m == NULL ) {
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
	return references_remove(references, argv[1], NULL);
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
		r = files_deleteandremove(dist->filesdb,filekey,!keepdirectories,FALSE);
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


struct remove_args {/*@temp@*/references refs; int count; /*@temp@*/ const char * const *names; bool_t *gotremoved; int todo;/*@temp@*/struct strlist *removedfiles;/*@temp@*/struct trackingdata *trackingdata;};

static retvalue remove_from_target(/*@temp@*/void *data, struct target *target,
		struct distribution *distribution) {
	retvalue result,r;
	int i;
	struct remove_args *d = data;

	result = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(result) ) {
		RET_UPDATE(distribution->status,result);
		return result;
	}

	result = RET_NOTHING;
	for( i = 0 ; i < d->count ; i++ ){
		r = target_removepackage(target,d->refs,d->names[i],d->removedfiles,d->trackingdata);
		if( RET_IS_OK(r) ) {
			if( ! d->gotremoved[i] )
				d->todo--;
			d->gotremoved[i] = TRUE;
		}
		RET_UPDATE(result,r);
	}
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	RET_UPDATE(distribution->status,result);
	return result;
}

ACTION_D(remove) {
	retvalue result,r;
	struct distribution *distribution;
	struct remove_args d;
	trackingdb tracks;
	struct trackingdata trackingdata;

	if( argc < 3  ) {
		fprintf(stderr,"reprepro [-C <component>] [-A <architecture>] [-T <type>] remove <codename> <package-names>\n");
		return RET_ERROR;
	}
	r = distribution_get(&distribution,confdir,argv[1]);
	assert( r != RET_NOTHING);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	if( distribution->tracking != dt_NONE ) {
		r = tracking_initialize(&tracks,dbdir,distribution);
		if( RET_WAS_ERROR(r) ) {
			(void)distribution_free(distribution);
			return r;
		}
		r = trackingdata_new(tracks,&trackingdata);
		if( RET_WAS_ERROR(r) ) {
			(void)distribution_free(distribution);
			(void)tracking_done(tracks);
			return r;
		}
		d.trackingdata = &trackingdata;
	} else {
		d.trackingdata = NULL;
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
		result = distribution_foreach_part(distribution,component,architecture,packagetype,remove_from_target,&d);
	d.refs = NULL;
	d.removedfiles = NULL;

	r = distribution_export(export, distribution,confdir,dbdir,distdir,filesdb);
	RET_ENDUPDATE(result,r);

	if( d.trackingdata != NULL ) {
		trackingdata_done(d.trackingdata);
		r = tracking_done(tracks);
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

static retvalue list_in_target(void *data, struct target *target,
		UNUSED(struct distribution *distribution)) {
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

	result = distribution_foreach_part(distribution,component,architecture,packagetype,list_in_target,(void*)argv[2]);
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

static retvalue listfilter_in_target(/*@temp@*/void *data, /*@temp@*/struct target *target,
		UNUSED(struct distribution *distribution)) {
	retvalue r,result;
	/*@temp@*/ struct listfilter d;

	result = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	d.target = target;
	d.condition = data;
	result = packages_foreach(target->packages,listfilterprint,&d);
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

	result = distribution_foreach_part(distribution,component,architecture,packagetype,listfilter_in_target,condition);
	term_free(condition);
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
		while( fgets(buffer,4999,stdin) != NULL ) {
			nl = strchr(buffer,'\n');
			if( nl == NULL ) {
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
			r = files_remove(filesdb, argv[i], FALSE);
			RET_UPDATE(ret,r);
		}

	} else
		while( fgets(buffer,4999,stdin) != NULL ) {
			nl = strchr(buffer,'\n');
			if( nl == NULL ) {
				return RET_ERROR;
			}
			*nl = '\0';
			r = files_remove(filesdb, buffer, FALSE);
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

	result = packages_foreach(packages,printout,NULL);

	r = packages_done(packages);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_F(export) {
	retvalue result,r;
	struct distribution *distributions,*d;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro export [<distributions>]\n");
		return RET_ERROR;
	}

	if( export == EXPORT_NEVER ) {
		fprintf(stderr, "Error: reprepro export incompatible with --export=never\n");
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

		r = distribution_fullexport(d,confdir,dbdir,distdir,filesdb);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && export != EXPORT_FORCE) {
			distribution_freelist(distributions);
			return r;
		}
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);
	return result;
}

/***********************update********************************/

ACTION_D(update) {
	retvalue result,r;
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
	assert( RET_IS_OK(result) );

	result = updates_calcindices(listdir,patterns,distributions,&u_distributions);
	if( RET_WAS_ERROR(result) ) {
		updates_freepatterns(patterns);
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}
	assert( RET_IS_OK(result) );

	if( !keepunneededlists ) {
		result = updates_clearlists(listdir,u_distributions);
	}
	if( !RET_WAS_ERROR(result) )
		result = updates_update(dbdir,methoddir,filesdb,references,u_distributions,nolistsdownload,skipold,dereferenced);
	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);

	r = distribution_exportandfreelist(export,distributions,
			confdir,dbdir,distdir, filesdb);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_D(predelete) {
	retvalue result,r;
	struct update_pattern *patterns;
	struct distribution *distributions;
	struct update_distribution *u_distributions;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro predelete [<distributions>]\n");
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
	assert( RET_IS_OK(result) );

	result = updates_calcindices(listdir,patterns,distributions,&u_distributions);
	if( RET_WAS_ERROR(result) ) {
		updates_freepatterns(patterns);
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}
	assert( RET_IS_OK(result) );

	if( !keepunneededlists ) {
		result = updates_clearlists(listdir,u_distributions);
	}
	if( !RET_WAS_ERROR(result) )
		result = updates_predelete(dbdir,methoddir,references,u_distributions,nolistsdownload,skipold,dereferenced);
	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);

	r = distribution_exportandfreelist(export,distributions,
			confdir,dbdir,distdir,filesdb);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_D(iteratedupdate) {
	retvalue result,r;
	struct update_pattern *patterns;
	struct distribution *distributions;
	struct update_distribution *u_distributions;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro iteratedupdate [<distributions>]\n");
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
		result = updates_iteratedupdate(confdir,dbdir,distdir,methoddir,filesdb,references,u_distributions,nolistsdownload,skipold,dereferenced,export);
	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);

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

	result = updates_checkupdate(dbdir,methoddir,u_distributions,nolistsdownload,skipold);

	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}
/***********************migrate*******************************/

ACTION_D(pull) {
	retvalue result,r;
	struct pull_rule *rules;
	struct pull_distribution *p;
	struct distribution *distributions,
	/* list of distributions only source but not target of a replication: */
		*sourceonly = NULL;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro pull [<distributions>]\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = pull_getrules(confdir,&rules);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}
	assert( RET_IS_OK(result) );

	result = pull_prepare(confdir,rules,distributions,&p,&sourceonly);
	if( RET_WAS_ERROR(result) ) {
		pull_freerules(rules);
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}
	result = pull_update(dbdir,filesdb,references,p,dereferenced);

	pull_freerules(rules);
	pull_freedistributions(p);
	r = distribution_freelist(sourceonly);
	RET_ENDUPDATE(result,r);

	r = distribution_exportandfreelist(export,distributions,
			confdir,dbdir,distdir,filesdb);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_N(checkpull) {
	retvalue result,r;
	struct pull_rule *rules;
	struct pull_distribution *p;
	struct distribution *distributions,
	/* list of distributions only source but not target of a replication: */
		*sourceonly = NULL;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro checkpull [<distributions>]\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = pull_getrules(confdir,&rules);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}
	assert( RET_IS_OK(result) );

	result = pull_prepare(confdir,rules,distributions,&p,&sourceonly);
	if( RET_WAS_ERROR(result) ) {
		pull_freerules(rules);
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}
	result = pull_checkupdate(dbdir,p);

	pull_freerules(rules);
	pull_freedistributions(p);
	r = distribution_freelist(sourceonly);
	RET_ENDUPDATE(result,r);

	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}

struct copy_data {
	struct distribution *destination;
	/*@temp@*/references refs;
	/*@temp@*/const char *name;
	/*@temp@*/struct strlist *removedfiles;
};

static retvalue copy(/*@temp@*/void *data, struct target *origtarget,
		struct distribution *distribution) {
	retvalue result,r;
	struct copy_data *d = data;
	char *chunk,*version;
	struct strlist filekeys;
	struct target *dsttarget;

	result = target_initpackagesdb(origtarget,dbdir);
	if( RET_WAS_ERROR(result) ) {
		RET_UPDATE(distribution->status,result);
		return result;
	}

	dsttarget = distribution_gettarget(d->destination, origtarget->component,
					origtarget->architecture,
					origtarget->packagetype);
	if( dsttarget == NULL ) {
		if( verbose > 2 )
			printf("Not looking into '%s' as no matching target in '%s'!\n",
					origtarget->identifier,
					d->destination->codename);
		result = RET_NOTHING;
	} else
		result = packages_get(origtarget->packages, d->name, &chunk);
	if( result == RET_NOTHING && verbose > 2 )
		printf("No instance of '%s' found in '%s'!\n",
				d->name, origtarget->identifier);
	r = target_closepackagesdb(origtarget);
	RET_ENDUPDATE(result,r);
	if( !RET_IS_OK(result) ) {
		return result;
	}

	result = origtarget->getversion(origtarget, chunk, &version);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		free(chunk);
		return result;
	}

	result = origtarget->getfilekeys(origtarget, chunk, &filekeys, NULL);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		free(chunk);
		free(version);
		return result;
	}
	if( verbose >= 1 ) {
		printf("Moving '%s' from '%s' to '%s'.\n",
				d->name,
				origtarget->identifier,
				dsttarget->identifier);
	}

	result = target_initpackagesdb(dsttarget,dbdir);
	if( RET_WAS_ERROR(result) ) {
		RET_UPDATE(d->destination->status,result);
		free(chunk);
		free(version);
		strlist_done(&filekeys);
		return result;
	}

	result = target_addpackage(dsttarget, d->refs, d->name, version, chunk,
			&filekeys, TRUE, d->removedfiles,
			NULL, '?');
	free(version);
	free(chunk);
	strlist_done(&filekeys);

	r = target_closepackagesdb(dsttarget);
	RET_ENDUPDATE(result,r);
	RET_UPDATE(d->destination->status,result);
	return result;
}

ACTION_D(copy) {
	struct distribution *destination,*source;
	retvalue result, r;
	struct copy_data d;
	int i;

	if( argc < 3 ) {
		fprintf(stderr,"reprepro [-C <component> ] [-A <architecture>] [-T <packagetype>] copy <destination-distribution> <source-distribution> <package-names to pull>\n");
		return RET_ERROR;
	}
	result = distribution_get(&destination,confdir,argv[1]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;
	result = distribution_get(&source,confdir,argv[2]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		distribution_free(destination);
		return result;
	}

	if( destination->tracking != dt_NONE ) {
		fprintf(stderr, "WARNING: copy does not yet support trackingdata and will ignore trackingdata in '%s'!\n", destination->codename);
	}

	d.destination = destination;
	d.refs = references;
	d.removedfiles = dereferenced;
	for( i = 3; i < argc ; i++ ) {
		d.name = argv[i];
		if( verbose > 0 )
			printf("Looking for '%s' in '%s' to be copied to '%s'...\n",
					d.name, source->codename,
					destination->codename);
		result = distribution_foreach_part(source,component,architecture,packagetype,copy,&d);
	}
	d.refs = NULL;
	d.removedfiles = NULL;
	d.destination = NULL;

	r = distribution_export(export,destination,confdir,dbdir,distdir,filesdb);
	RET_ENDUPDATE(result,r);

	distribution_free(source);
	distribution_free(destination);
	return result;

}

/***********************rereferencing*************************/
struct data_binsrcreref { /*@temp@*/const struct distribution *distribution; /*@temp@*/references refs;};

static retvalue reref(void *data,struct target *target,UNUSED(struct distribution *di)) {
	retvalue result,r;
	struct data_binsrcreref *d = data;

	result = target_initpackagesdb(target,dbdir);
	if( !RET_WAS_ERROR(result) ) {
		result = target_rereference(target,d->refs);
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
				reref,&dat);
		dat.refs = NULL;
		dat.distribution = NULL;
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}
/***************************retrack****************************/
struct data_binsrctrack { /*@temp@*/const struct distribution *distribution; /*@temp@*/references refs; trackingdb tracks;};

static retvalue retrack(void *data,struct target *target,UNUSED(struct distribution *di)) {
	retvalue result,r;
	struct data_binsrctrack *d = data;

	result = target_initpackagesdb(target,dbdir);
	if( !RET_WAS_ERROR(result) ) {
		result = target_retrack(target,d->tracks,d->refs);
		r = target_closepackagesdb(target);
		RET_ENDUPDATE(result,r);
	}
	return result;
}

ACTION_R(retrack) {
	retvalue result,r;
	struct distribution *distributions,*d;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro retrack [<distributions>]\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = distributions ; d != NULL ; d = d->next ) {
		struct data_binsrctrack dat;

		if( verbose > 0 ) {
			fprintf(stderr,"Chasing %s...\n",d->codename);
		}
		dat.distribution = d;
		dat.refs = references;
		r = tracking_initialize(&dat.tracks,dbdir,d);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
			continue;
		}
		r = tracking_clearall(dat.tracks);
		RET_UPDATE(result,r);
		r = references_remove(references,d->codename, NULL);
		RET_UPDATE(result,r);

		r = distribution_foreach_part(d,component,architecture,packagetype,
				retrack,&dat);
		RET_UPDATE(result,r);
		dat.refs = NULL;
		dat.distribution = NULL;
		r = tracking_done(dat.tracks);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(result) )
			break;
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_D_U(removetrack) {
	retvalue result,r;
	struct distribution *distribution;
	trackingdb tracks;

	if( argc != 4 ) {
		fprintf(stderr,"reprepro removetrack <distribution> <sourcename> <version>\n");
		return RET_ERROR;
	}
	result = distribution_get(&distribution,confdir,argv[1]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;
	r = tracking_initialize(&tracks,dbdir,distribution);
	if( RET_WAS_ERROR(r) ) {
		distribution_free(distribution);
		return r;
	}

	result = tracking_remove(tracks,argv[2],argv[3],references,dereferenced);

	r = tracking_done(tracks);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);
	return result;
}

ACTION_D(cleartracks) {
	retvalue result,r;
	struct distribution *distributions,*d;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro cleartracks [<distributions>]\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = distributions ; d != NULL ; d = d->next ) {
		trackingdb tracks;

		if( verbose > 0 ) {
			fprintf(stderr,"Deleting all tracks for %s...\n",d->codename);
		}
		r = tracking_initialize(&tracks,dbdir,d);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
			continue;
		}
		r = tracking_clearall(tracks);
		RET_UPDATE(result,r);
		r = references_remove(references, d->codename, dereferenced);
		RET_UPDATE(result,r);
		r = tracking_done(tracks);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(result) )
			break;
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}
ACTION_N(dumptracks) {
	retvalue result,r;
	struct distribution *distributions,*d;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro dumptracks [<distributions>]\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = distributions ; d != NULL ; d = d->next ) {
		trackingdb tracks;

		r = tracking_initialize(&tracks,dbdir,d);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
			continue;
		}
		r = tracking_printall(tracks);
		RET_UPDATE(result,r);
		r = tracking_done(tracks);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(result) )
			break;
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);

	return result;
}
/***********************checking*************************/
struct data_check { /*@temp@*/references references; /*@temp@*/filesdb filesdb;};

static retvalue check_target(void *data,struct target *target,UNUSED(struct distribution *di)) {
	struct data_check *d = data;
	retvalue result,r;

	r = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	result = target_check(target,d->filesdb,d->references);
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

		dat.references = references;
		dat.filesdb = filesdb;

		r = distribution_foreach_part(d,component,architecture,packagetype,check_target,&dat);
		dat.references = NULL;
		dat.filesdb = NULL;
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
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
/*****************reapplying override info***************/

static retvalue reoverride_target(void *data,struct target *target,struct distribution *distribution) {
	const struct distribution *d = data;
	retvalue result,r;

	r = target_initpackagesdb(target,dbdir);
	if( RET_WAS_ERROR(r) )
		return r;
	result = target_reoverride(target,d);
	r = target_closepackagesdb(target);
	RET_ENDUPDATE(result,r);
	RET_UPDATE(distribution->status, result);
	return result;
}

ACTION_F(reoverride) {
	retvalue result,r;
	struct distribution *distributions,*d;

	if( argc < 1 ) {
		fprintf(stderr,"reprepro [-T ...] [-C ...] [-A ...] reoverride [<distributions>]\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = distributions ; d != NULL ; d = d->next ) {

		if( verbose > 0 ) {
			fprintf(stderr,"Reapplying override to %s...\n",d->codename);
		}

		r = distribution_loadalloverrides(d,overridedir);
		if( RET_IS_OK(r) ) {
			r = distribution_foreach_part(d,component,architecture,packagetype,reoverride_target,d);
			distribution_unloadoverrides(d);
		} else if( r == RET_NOTHING ) {
			fprintf(stderr,"No override files, thus nothing to do for %s.\n",d->codename);
		}
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	r = distribution_exportandfreelist(export,distributions,confdir,dbdir,distdir,filesdb);
	RET_ENDUPDATE(result,r);

	return result;
}

/***********************include******************************************/

ACTION_D(includedeb) {
	retvalue result,r;
	struct distribution *distribution;
	bool_t isudeb;
	trackingdb tracks;

	if( argc != 3 ) {
		fprintf(stderr,"reprepro [--delete] include[u]deb <distribution> <package>\n");
		return RET_ERROR;
	}
	if( architecture != NULL && strcmp(architecture,"source") == 0 ) {
		fprintf(stderr,"Error: -A source is not possible with includedeb!\n");
		return RET_ERROR;
	}
	if( strcmp(argv[0],"includeudeb") == 0 ) {
		isudeb = TRUE;
		if( packagetype != NULL && strcmp(packagetype,"udeb") != 0 ) {
			fprintf(stderr,"Calling includeudeb with a -T different from 'udeb' makes no sense!\n");
			return RET_ERROR;
		}
	} else if( strcmp(argv[0],"includedeb") == 0 ) {
		isudeb = FALSE;
		if( packagetype != NULL && strcmp(packagetype,"deb") != 0 ) {
			fprintf(stderr,"Calling includedeb with -T something where something is not 'deb' makes no sense!\n");
			return RET_ERROR;
		}

	} else {
		fprintf(stderr,"Internal error with command parsing!\n");
		return RET_ERROR;
	}
	if( isudeb ) {
		if( !endswith(argv[2],".udeb") && !IGNORING_(extension,
			"includeudeb called with a file not ending with '.udeb'\n") )
			return RET_ERROR;
	} else {
		if( !endswith(argv[2],".deb") && !IGNORING_(extension,
			"includedeb called with a file not ending with '.deb'\n") )
			return RET_ERROR;
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	if( isudeb )
		result = override_read(overridedir,distribution->udeb_override,&distribution->overrides.udeb);
	else
		result = override_read(overridedir,distribution->deb_override,&distribution->overrides.deb);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_free(distribution);
		RET_ENDUPDATE(result,r);
		return result;
	}

	// TODO: same for component? (depending on type?)
	if( architecture != NULL && !strlist_in(&distribution->architectures,architecture) ){
		fprintf(stderr,"Cannot force into the architecture '%s' not available in '%s'!\n",architecture,distribution->codename);
		(void)distribution_free(distribution);
		return RET_ERROR;
	}

	if( distribution->tracking != dt_NONE ) {
		result = tracking_initialize(&tracks,dbdir,distribution);
		if( RET_WAS_ERROR(result) ) {
			r = distribution_free(distribution);
			RET_ENDUPDATE(result,r);
			return result;
		}
	} else {
		tracks = NULL;
	}

	result = deb_add(dbdir,references,filesdb,component,architecture,
			section,priority,isudeb?"udeb":"deb",distribution,argv[2],
			NULL,NULL,delete,
			dereferenced,tracks);
	RET_UPDATE(distribution->status, result);

	distribution_unloadoverrides(distribution);

	r = tracking_done(tracks);
	RET_ENDUPDATE(result,r);

	r = distribution_export(export,distribution,confdir,dbdir,distdir,filesdb);
	RET_ENDUPDATE(result,r);

	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);

	return result;
}


ACTION_D(includedsc) {
	retvalue result,r;
	struct distribution *distribution;
	trackingdb tracks;

	if( argc != 3 ) {
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
	if( !endswith(argv[2],".dsc") && !IGNORING_(extension,
				"includedsc called with a file not ending with '.dsc'\n") )
		return RET_ERROR;

	result = distribution_get(&distribution,confdir,argv[1]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;
	result = override_read(overridedir,distribution->dsc_override,&distribution->overrides.dsc);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_free(distribution);
		RET_ENDUPDATE(result,r);
		return result;
	}

	if( distribution->tracking != dt_NONE ) {
		result = tracking_initialize(&tracks,dbdir,distribution);
		if( RET_WAS_ERROR(result) ) {
			r = distribution_free(distribution);
			RET_ENDUPDATE(result,r);
			return result;
		}
	} else {
		tracks = NULL;
	}

	result = dsc_add(dbdir,references,filesdb,component,section,priority,distribution,argv[2],delete,dereferenced,tracks);

	distribution_unloadoverrides(distribution);
	r = tracking_done(tracks);
	RET_ENDUPDATE(result,r);
	r = distribution_export(export,distribution,confdir,dbdir,distdir,filesdb);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_D(include) {
	retvalue result,r;
	struct distribution *distribution;
	trackingdb tracks;

	if( argc != 3 ) {
		fprintf(stderr,"reprepro [--delete] include <distribution> <.changes-file>\n");
		return RET_ERROR;
	}
	if( !endswith(argv[2],".changes") && !IGNORING_(extension,
				"include called with a file not ending with '.changes'\n"
				"(Did you mean includedeb or includedsc?)\n") )
		return RET_ERROR;

	if( architecture != NULL && packagetype != NULL ) {
		if( strcmp(packagetype,"dsc") == 0 ) {
			if( strcmp(architecture,"source") != 0 ) {
				fprintf(stderr,"Error: Only -A source is possible with -T dsc!\n");
				return RET_ERROR;
			}
		} else {
			if( strcmp(architecture,"source") == 0 ) {
				fprintf(stderr,"Error: -A source is not possible with -T deb or -T udeb!\n");
				return RET_ERROR;
			}
		}
	}

	result = distribution_get(&distribution,confdir,argv[1]);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = distribution_loadalloverrides(distribution,overridedir);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_free(distribution);
		RET_ENDUPDATE(result,r);
		return result;
	}

	if( distribution->tracking != dt_NONE ) {
		result = tracking_initialize(&tracks,dbdir,distribution);
		if( RET_WAS_ERROR(result) ) {
			r = distribution_free(distribution);
			RET_ENDUPDATE(result,r);
			return result;
		}
	} else {
		tracks = NULL;
	}
	result = distribution_loaduploaders(distribution, confdir);
	if( RET_WAS_ERROR(result) ) {
		r = tracking_done(tracks);
		RET_ENDUPDATE(result,r);
		r = distribution_free(distribution);
		RET_ENDUPDATE(result,r);
		return result;
	}
	result = changes_add(dbdir,tracks,references,filesdb,packagetype,component,architecture,section,priority,distribution,argv[2],delete,dereferenced);

	distribution_unloadoverrides(distribution);
	distribution_unloaduploaders(distribution);
	r = tracking_done(tracks);
	RET_ENDUPDATE(result,r);
	r = distribution_export(export,distribution,confdir,dbdir,distdir,filesdb);
	RET_ENDUPDATE(result,r);
	r = distribution_free(distribution);
	RET_ENDUPDATE(result,r);

	return result;
}

/***********************createsymlinks***********************************/

ACTION_N(createsymlinks) {
	retvalue result,r;
	struct distribution *distributions,*d,*d2;

	r = dirs_make_recursive(distdir);
	if( RET_WAS_ERROR(r) )
		return r;

	result = distribution_getmatched(confdir,argc-1,argv+1,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = distributions ; d != NULL ; d = d->next ) {
		char *linkname,*buffer;
		size_t bufsize;
		int ret;

		if( d->suite == NULL )
			continue;
		r = RET_NOTHING;
		for( d2 = distributions ; d2 != NULL ; d2 = d2->next ) {
			if( d!=d2 && d2->suite!=NULL &&strcmp(d->suite,d2->suite)==0) {
				fprintf(stderr,
"Not linking %s->%s due to conflict with %s->%s\n",
					d->suite,d->codename,
					d2->suite,d2->codename);
				r = RET_ERROR;
			} else if( strcmp(d->suite,d2->codename)==0) {
				fprintf(stderr,
"Not linking %s->%s due to conflict with %s\n",
					d->suite,d->codename,d2->codename);
				r = RET_ERROR;
			}
		}
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			continue;
		}

		linkname = calc_dirconcat(distdir,d->suite);
		bufsize = strlen(d->codename)+10;
		buffer = calloc(1,bufsize);
		if( linkname == NULL || buffer == NULL ) {
			free(linkname);free(buffer);
			fputs("Out of Memory!\n",stderr);
			return RET_ERROR_OOM;
		}

		ret = readlink(linkname,buffer,bufsize-4);
		if( ret < 0 && errno == ENOENT ) {
			ret = symlink(d->codename,linkname);
			if( ret != 0 ) {
				r = RET_ERRNO(errno);
				fprintf(stderr,"Error creating symlink %s->%s: %d=%m\n",linkname,d->codename,errno);
				RET_UPDATE(result,r);
			} else {
				if( verbose > 0 ) {
					printf("Created %s->%s\n",linkname,d->codename);
				}
				RET_UPDATE(result,RET_OK);
			}
		} else if( ret >= 0 ) {
			buffer[ret] = '\0';
			if( ret >= ((int)bufsize)-4 ) {
				buffer[bufsize-4]='.';
				buffer[bufsize-3]='.';
				buffer[bufsize-2]='.';
				buffer[bufsize-1]='\0';
			}
			if( strcmp(buffer,d->codename) == 0 ) {
				if( verbose > 2 ) {
					printf("Already ok: %s->%s\n",linkname,d->codename);
				}
				RET_UPDATE(result,RET_OK);
			} else {
				if( delete <= 0 ) {
					fprintf(stderr,"Cannot create %s as already pointing to %s instead of %s,\n use --delete to delete the old link before creating an new one.\n",linkname,buffer,d->codename);
					RET_UPDATE(result,RET_ERROR);
				} else {
					unlink(linkname);
					ret = symlink(d->codename,linkname);
					if( ret != 0 ) {
						r = RET_ERRNO(errno);
						fprintf(stderr,"Error creating symlink %s->%s: %d=%m\n",linkname,d->codename,errno);
						RET_UPDATE(result,r);
					} else {
						if( verbose > 0 ) {
							printf("Replaced %s->%s\n",linkname,d->codename);
						}
						RET_UPDATE(result,RET_OK);
					}

				}
			}
		} else {
			r = RET_ERRNO(errno);
			fprintf(stderr,"Error checking %s, perhaps not a symlink?: %d=%m\n",linkname,errno);
			RET_UPDATE(result,r);
		}

		RET_UPDATE(result,r);
	}
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);
	return result;
}

/***********************clearvanished***********************************/

static retvalue docount(void *data,UNUSED(const char *a),UNUSED(const char *b)) {
	long int *p = data;

	(*p)++;
	return RET_OK;
}


ACTION_D_UU(clearvanished) {
	retvalue result,r;
	struct distribution *distributions,*d;
	struct strlist identifiers;
	bool_t *inuse;
	int i;

	if( argc != 1 ) {
		fprintf(stderr,"reprepro [--delete] clearvanished\n");
		return RET_ERROR;
	}

	result = distribution_getmatched(confdir,0,NULL,&distributions);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = packages_getdatabases(dbdir, &identifiers);
	if( RET_WAS_ERROR(result) ) {
		r = distribution_freelist(distributions);
		RET_ENDUPDATE(result,r);
		return result;
	}

	inuse = calloc(identifiers.count,sizeof(bool_t));
	if( inuse == NULL ) {
		strlist_done(&identifiers);
		(void)distribution_freelist(distributions);
		return RET_ERROR_OOM;
	}
	for( d = distributions; d != NULL ; d = d->next ) {
		struct target *t;
		for( t = d->targets; t != NULL ; t = t->next ) {
			int i = strlist_ofs(&identifiers, t->identifier);
			if( i >= 0 ) {
				inuse[i] = TRUE;
				if( verbose > 6 )
					fprintf(stderr,
"Marking '%s' as used.\n", t->identifier);
			} else if( verbose > 3 ){
				fprintf(stderr,
"Strange, '%s' does not appear in packages.db yet.\n", t->identifier);

			}
		}
	}
	for( i = 0 ; i < identifiers.count ; i ++ ) {
		const char *identifier = identifiers.values[i];
		if( inuse[i] )
			continue;
		if( delete <= 0 ) {
			long int count = 0;
			packagesdb db;
			r = packages_initialize(&db, dbdir, identifier);
			if( !RET_WAS_ERROR(r) )
				r = packages_foreach(db, docount, &count);
			if( !RET_WAS_ERROR(r) )
				r = packages_done(db);
			if( count > 0 ) {
				fprintf(stderr,
"There are still %ld packages in '%s', not removing (give --delete to do so)!\n", count, identifier);
				continue;
			}
		}
		fprintf(stderr,
"Deleting vanished identifier '%s'.\n", identifier);
		/* derference anything left */
		references_remove(references, identifier, dereferenced);
		/* remove the database */
		packages_drop(dbdir, identifier);
	}
	free(inuse);

	strlist_done(&identifiers);
	r = distribution_freelist(distributions);
	RET_ENDUPDATE(result,r);
	return result;
}

ACTION_N(versioncompare) {
	retvalue r;
	int i;

	if( argc != 3 ) {
		fprintf(stderr,"reprepro versioncompare <version> <version>\n");
		return RET_ERROR;
	}
	r = properversion(argv[1]);
	if( RET_WAS_ERROR(r) )
		fprintf(stderr, "'%s' is not a proper version!\n", argv[1]);
	r = properversion(argv[2]);
	if( RET_WAS_ERROR(r) )
		fprintf(stderr, "'%s' is not a proper version!\n", argv[2]);
	r = dpkgversions_cmp(argv[1],argv[2],&i);
	if( RET_IS_OK(r) ) {
		if( i < 0 ) {
			fprintf(stderr, "'%s' is smaller than '%s'.\n",
						argv[1], argv[2]);
		} else if( i > 0 ) {
			fprintf(stderr, "'%s' is larger than '%s'.\n",
					argv[1], argv[2]);
		} else
			fprintf(stderr, "'%s' is the same as '%s'.\n",
					argv[1], argv[2]);
	}
	return r;
}
/***********************import***********************************/
ACTION_D(import) {
	retvalue result,r;
	struct distribution *distributions;

	if( argc != 2 ) {
		fprintf(stderr,"reprepro import <import-name>\n");
		return RET_ERROR;
	}

	r = distribution_getmatched(confdir,0,NULL,&distributions);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	result = process_incoming(mirrordir, confdir, filesdb, dbdir, references, dereferenced, distributions, argv[1]);

	r = distribution_exportandfreelist(export,distributions,
			confdir,dbdir,distdir, filesdb);
	RET_ENDUPDATE(result,r);

	return result;
}

/**********************/
/* lock file handling */
/**********************/

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
	{"__extractfilelist",	A_N(extractfilelist)},
	{"_detect", 		A_F(detect)},
	{"_forget", 		A_F(forget)},
	{"_listmd5sums",	A_F(listmd5sums)},
	{"_addmd5sums",		A_F(addmd5sums)},
	{"_dumpcontents", 	A_N(dumpcontents)},
	{"_removereferences", 	A_R(removereferences)},
	{"_addreference", 	A_R(addreference)},
	{"_versioncompare",	A_N(versioncompare)},
	{"_fakeemptyfilelist",	A_F(fakeemptyfilelist)},
	{"remove", 		A_D(remove)},
	{"list", 		A_N(list)},
	{"listfilter", 		A_N(listfilter)},
	{"createsymlinks", 	A_N(createsymlinks)},
	{"export", 		A_F(export)},
	{"check", 		A_RF(check)},
	{"reoverride", 		A_F(reoverride)},
	{"checkpool", 		A_F(checkpool)},
	{"rereference", 	A_R(rereference)},
	{"dumpreferences", 	A_R(dumpreferences)},
	{"dumpunreferenced", 	A_RF(dumpunreferenced)},
	{"deleteunreferenced", 	A_RF(deleteunreferenced)},
	{"retrack",	 	A_R(retrack)},
	{"dumptracks",	 	A_N(dumptracks)},
	{"cleartracks",	 	A_D(cleartracks)},
	{"removetrack",		A_D(removetrack)},
	{"update",		A_D(update)},
	{"iteratedupdate",	A_D(iteratedupdate)},
	{"checkupdate",		A_N(checkupdate)},
	{"predelete",		A_D(predelete)},
	{"pull",		A_D(pull)},
	{"copy",		A_D(copy)},
	{"checkpull",		A_N(checkpull)},
	{"includedeb",		A_D(includedeb)},
	{"includeudeb",		A_D(includedeb)},
	{"includedsc",		A_D(includedsc)},
	{"include",		A_D(include)},
	{"generatefilelists",	A_F(generatefilelists)},
	{"clearvanished",	A_D(clearvanished)},
	{"import",		A_D(import)},
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
				strlist_init(&dereferencedfilekeys);
			}

			if( !interrupted() ) {
				result = action->start(references,filesdb,
					deletederef?&dereferencedfilekeys:NULL,
					argc,argv);

				if( deletederef ) {
					if( dereferencedfilekeys.count > 0 ) {
					    if( RET_IS_OK(result) && !interrupted() ) {
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

enum { LO_DELETE=1,
LO_KEEPUNREFERENCED,
LO_KEEPUNNEEDEDLISTS,
LO_NOTHINGISERROR,
LO_NOLISTDOWNLOAD,
LO_ASKPASSPHRASE,
LO_KEEPDIRECTORIES,
LO_SKIPOLD,
LO_NODELETE,
LO_NOKEEPUNREFERENCED,
LO_NOKEEPUNNEEDEDLISTS,
LO_NONOTHINGISERROR,
LO_LISTDOWNLOAD,
LO_NOASKPASSPHRASE,
LO_NOKEEPDIRECTORIES,
LO_NOSKIPOLD,
LO_EXPORT,
LO_DISTDIR,
LO_DBDIR,
LO_LISTDIR,
LO_OVERRIDEDIR,
LO_CONFDIR,
LO_METHODDIR,
LO_VERSION,
LO_UNIGNORE};
static int longoption = 0;
const char *programname;

static void setexport(const char *optarg) {
	if( strcasecmp(optarg, "never") == 0 ) {
		CONFIGSET(export, EXPORT_NEVER);
		return;
	}
	if( strcasecmp(optarg, "changed") == 0 ) {
		CONFIGSET(export, EXPORT_CHANGED);
		return;
	}
	if( strcasecmp(optarg, "normal") == 0 ) {
		CONFIGSET(export, EXPORT_NORMAL);
		return;
	}
	if( strcasecmp(optarg, "force") == 0 ) {
		CONFIGSET(export, EXPORT_FORCE);
		return;
	}
	fprintf(stderr,"Error: --export needs an argument of 'never', 'normal' or 'force', but got '%s'\n", optarg);
	exit(EXIT_FAILURE);
}

static void handle_option(int c,const char *optarg) {
	retvalue r;

	switch( c ) {
		case 'h':
			printf(
"reprepro - Produce and Manage a Debian package repository\n\n"
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
" dumpunreferenced:   Print registered files without reference\n"
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
"       Include the given upload.\n"
" includedeb <distribution> <.deb-file>\n"
"       Include the given binary package.\n"
" includeudeb <distribution> <.udeb-file>\n"
"       Include the given installer binary package.\n"
" includedsc <distribution> <.dsc-file>\n"
"       Include the given source package.\n"
" list <distribution> <package-name>\n"
"       List all packages by the given name occurring in the given distribution.\n"
" listfilter <distribution> <condition>\n"
"       List all packages in the given distribution matching the condition.\n"
" clearvanished\n"
"       Remove everything no longer referenced in the distributions config file.\n"
"\n");
			exit(EXIT_SUCCESS);
		case '\0':
			switch( longoption ) {
				case LO_UNIGNORE:
					r = set_ignore(optarg,FALSE,config_state);
					if( RET_WAS_ERROR(r) ) {
						exit(EXIT_FAILURE);
					}
					break;
				case LO_DELETE:
					delete++;
					break;
				case LO_NODELETE:
					delete--;
					break;
				case LO_KEEPUNREFERENCED:
					CONFIGSET(keepunreferenced,TRUE);
					break;
				case LO_NOKEEPUNREFERENCED:
					CONFIGSET(keepunreferenced,FALSE);
					break;
				case LO_KEEPUNNEEDEDLISTS:
					CONFIGSET(keepunneededlists,TRUE);
					break;
				case LO_NOKEEPUNNEEDEDLISTS:
					CONFIGSET(keepunneededlists,FALSE);
					break;
				case LO_KEEPDIRECTORIES:
					CONFIGSET(keepdirectories,TRUE);
					break;
				case LO_NOKEEPDIRECTORIES:
					CONFIGSET(keepdirectories,FALSE);
					break;
				case LO_NOTHINGISERROR:
					CONFIGSET(nothingiserror,TRUE);
					break;
				case LO_NONOTHINGISERROR:
					CONFIGSET(nothingiserror,FALSE);
					break;
				case LO_NOLISTDOWNLOAD:
					CONFIGSET(nolistsdownload,TRUE);
					break;
				case LO_LISTDOWNLOAD:
					CONFIGSET(nolistsdownload,FALSE);
					break;
				case LO_ASKPASSPHRASE:
					CONFIGSET(askforpassphrase,TRUE);
					break;
				case LO_NOASKPASSPHRASE:
					CONFIGSET(askforpassphrase,FALSE);
					break;
				case LO_SKIPOLD:
					CONFIGSET(skipold,TRUE);
					break;
				case LO_NOSKIPOLD:
					CONFIGSET(skipold,FALSE);
					break;
				case LO_EXPORT:
					setexport(optarg);
					break;
				case LO_DISTDIR:
					CONFIGDUP(distdir,optarg);
					break;
				case LO_DBDIR:
					CONFIGDUP(dbdir,optarg);
					break;
				case LO_LISTDIR:
					CONFIGDUP(listdir,optarg);
					break;
				case LO_OVERRIDEDIR:
					CONFIGDUP(overridedir,optarg);
					break;
				case LO_CONFDIR:
					CONFIGDUP(confdir,optarg);
					break;
				case LO_METHODDIR:
					CONFIGDUP(methoddir,optarg);
					break;
				case LO_VERSION:
					fprintf(stderr,"%s: This is " PACKAGE " version " VERSION "\n",programname);
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
			fprintf(stderr, "Ignoring no longer existing option -f/--force!\n");
			break;
		case 'b':
			CONFIGDUP(mirrordir,optarg);
			break;
		case 'i':
			r = set_ignore(optarg,TRUE,config_state);
			if( RET_WAS_ERROR(r) ) {
				exit(EXIT_FAILURE);
			}
			break;
		case 'C':
			if( component != NULL &&
					strcmp(component,optarg) != 0) {
				fprintf(stderr,"Multiple '-C' are not supported!\n");
				exit(EXIT_FAILURE);
			}
			CONFIGDUP(component,optarg);
			break;
		case 'A':
			if( architecture != NULL &&
					strcmp(architecture,optarg) != 0) {
				fprintf(stderr,"Multiple '-A's are not supported!\n");
				exit(EXIT_FAILURE);
			}
			CONFIGDUP(architecture,optarg);
			break;
		case 'T':
			if( packagetype != NULL &&
					strcmp(packagetype,optarg) != 0) {
				fprintf(stderr,"Multiple '-T's are not supported!\n");
				exit(EXIT_FAILURE);
			}
			CONFIGDUP(packagetype,optarg);
			break;
		case 'S':
			if( section != NULL &&
					strcmp(section,optarg) != 0) {
				fprintf(stderr,"Multiple '-S' are not supported!\n");
				exit(EXIT_FAILURE);
			}
			CONFIGDUP(section,optarg);
			break;
		case 'P':
			if( priority != NULL &&
					strcmp(priority,optarg) != 0) {
				fprintf(stderr,"Multiple '-P's are mpt supported!\n");
				exit(EXIT_FAILURE);
			}
			CONFIGDUP(priority,optarg);
			break;
		case '?':
			/* getopt_long should have already given an error msg */
			exit(EXIT_FAILURE);
		default:
			fprintf (stderr,"Not supported option '-%c'\n", c);
			exit(EXIT_FAILURE);
	}
}

static volatile bool_t was_interrupted = FALSE;
static bool_t interruption_printed = FALSE;

bool_t interrupted(void) {
	if( was_interrupted ) {
		if( !interruption_printed ) {
			interruption_printed = TRUE;
			fprintf(stderr, "\n\nInterruption in progress, interrupt again to force-stop it (and risking database corruption!)\n\n");
		}
		return TRUE;
	} else
		return FALSE;
}

static void interrupt_signaled(int signal) /*__attribute__((signal))*/;
static void interrupt_signaled(UNUSED(int signal)) {
	was_interrupted = TRUE;
}

int main(int argc,char *argv[]) {
	static struct option longopts[] = {
		{"delete", no_argument, &longoption,LO_DELETE},
		{"nodelete", no_argument, &longoption,LO_NODELETE},
		{"basedir", required_argument, NULL, 'b'},
		{"ignore", required_argument, NULL, 'i'},
		{"unignore", required_argument, &longoption, LO_UNIGNORE},
		{"noignore", required_argument, &longoption, LO_UNIGNORE},
		{"methoddir", required_argument, &longoption, LO_METHODDIR},
		{"distdir", required_argument, &longoption, LO_DISTDIR},
		{"dbdir", required_argument, &longoption, LO_DBDIR},
		{"listdir", required_argument, &longoption, LO_LISTDIR},
		{"overridedir", required_argument, &longoption, LO_OVERRIDEDIR},
		{"confdir", required_argument, &longoption, LO_CONFDIR},
		{"section", required_argument, NULL, 'S'},
		{"priority", required_argument, NULL, 'P'},
		{"component", required_argument, NULL, 'C'},
		{"architecture", required_argument, NULL, 'A'},
		{"type", required_argument, NULL, 'T'},
		{"help", no_argument, NULL, 'h'},
		{"verbose", no_argument, NULL, 'v'},
		{"version", no_argument, &longoption, LO_VERSION},
		{"nothingiserror", no_argument, &longoption, LO_NOTHINGISERROR},
		{"nolistsdownload", no_argument, &longoption, LO_NOLISTDOWNLOAD},
		{"keepunreferencedfiles", no_argument, &longoption, LO_KEEPUNREFERENCED},
		{"keepunneededlists", no_argument, &longoption, LO_KEEPUNNEEDEDLISTS},
		{"keepdirectories", no_argument, &longoption, LO_KEEPDIRECTORIES},
		{"ask-passphrase", no_argument, &longoption, LO_ASKPASSPHRASE},
		{"nonothingiserror", no_argument, &longoption, LO_NONOTHINGISERROR},
		{"nonolistsdownload", no_argument, &longoption, LO_LISTDOWNLOAD},
		{"listsdownload", no_argument, &longoption, LO_LISTDOWNLOAD},
		{"nokeepunreferencedfiles", no_argument, &longoption, LO_NOKEEPUNREFERENCED},
		{"nokeepunneededlists", no_argument, &longoption, LO_NOKEEPUNNEEDEDLISTS},
		{"nokeepdirectories", no_argument, &longoption, LO_NOKEEPDIRECTORIES},
		{"noask-passphrase", no_argument, &longoption, LO_NOASKPASSPHRASE},
		{"skipold", no_argument, &longoption, LO_SKIPOLD},
		{"noskipold", no_argument, &longoption, LO_NOSKIPOLD},
		{"nonoskipold", no_argument, &longoption, LO_SKIPOLD},
		{"force", no_argument, NULL, 'f'},
		{"export", required_argument, &longoption, LO_EXPORT},
		{NULL, 0, NULL, 0}
	};
	const struct action *a;
	retvalue r;
	int c;
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_ONESHOT;
	sa.sa_handler = interrupt_signaled;
	(void)sigaction(SIGTERM, &sa, NULL);
	(void)sigaction(SIGABRT, &sa, NULL);
	(void)sigaction(SIGINT, &sa, NULL);
	(void)sigaction(SIGQUIT, &sa, NULL);

	programname = argv[0];

	init_ignores();

	config_state = CONFIG_OWNER_CMDLINE;

	if( interrupted() )
		exit(EXIT_RET(RET_ERROR_INTERUPTED));

	while( (c = getopt_long(argc,argv,"+fVvhb:P:i:A:C:S:T:",longopts,NULL)) != -1 ) {
		handle_option(c,optarg);
	}
	if( optind >= argc ) {
		fprintf(stderr,"No action given. (see --help for available options and actions)\n");
		exit(EXIT_FAILURE);
	}
	if( interrupted() )
		exit(EXIT_RET(RET_ERROR_INTERUPTED));

	/* only for this CONFIG_OWNER_ENVIRONMENT is a bit stupid,
	 * but perhaps it gets more... */
	config_state = CONFIG_OWNER_ENVIRONMENT;
	if( mirrordir == NULL && getenv("REPREPRO_BASE_DIR") != NULL ) {
		CONFIGDUP(mirrordir,getenv("REPREPRO_BASE_DIR"));
	}
	if( confdir == NULL && getenv("REPREPRO_CONFIG_DIR") != NULL ) {
		CONFIGDUP(confdir,getenv("REPREPRO_CONFIG_DIR"));
	}

	if( mirrordir == NULL ) {
		mirrordir=strdup(STD_BASE_DIR);
	}
	if( confdir == NULL && mirrordir != NULL )
		confdir=calc_dirconcat(mirrordir,"conf");

	if( mirrordir == NULL || confdir == NULL ) {
		(void)fputs("Out of Memory!\n",stderr);
		exit(EXIT_FAILURE);
	}

	config_state = CONFIG_OWNER_FILE;
	optionsfile_parse(confdir,longopts,handle_option);

	/* basedir might have changed, so recalculate */
	if( owner_confdir == CONFIG_OWNER_DEFAULT ) {
		free(confdir);
		confdir=calc_dirconcat(mirrordir,"conf");
	}

	if( methoddir == NULL )
		methoddir = strdup(STD_METHOD_DIR);
	if( distdir == NULL )
		distdir=calc_dirconcat(mirrordir,"dists");
	if( dbdir == NULL )
		dbdir=calc_dirconcat(mirrordir,"db");
	if( listdir == NULL )
		listdir=calc_dirconcat(mirrordir,"lists");
	if( overridedir == NULL )
		overridedir=calc_dirconcat(mirrordir,"override");
	if( distdir == NULL || dbdir == NULL || listdir == NULL
			|| confdir == NULL || overridedir == NULL || methoddir == NULL) {
		(void)fputs("Out of Memory!\n",stderr);
		exit(EXIT_FAILURE);
	}
	if( interrupted() )
		exit(EXIT_RET(RET_ERROR_INTERUPTED));
	a = all_actions;
	while( a->name != NULL ) {
		if( strcasecmp(a->name,argv[optind]) == 0 ) {
			signature_init(askforpassphrase);
			r = callaction(a,argc-optind,(const char**)argv+optind);
			/* yeah, freeing all this stuff before exiting is
			 * stupid, but it makes valgrind logs easier
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
			if( RET_WAS_ERROR(r) ) {
				if( r == RET_ERROR_OOM )
					fputs("Out of Memory!\n",stderr);
				else if( verbose >= 0 )
					fputs("There have been errors!\n",stderr);
			}
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

