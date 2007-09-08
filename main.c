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
#include <limits.h>
#include <unistd.h>
#include <stdint.h>
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
#include "database.h"
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
#include "log.h"

#ifndef STD_BASE_DIR
#define STD_BASE_DIR "."
#endif
#ifndef STD_METHOD_DIR
#define STD_METHOD_DIR "/usr/lib/apt/methods"
#endif

#ifndef LLONG_MAX
#define LLONG_MAX __LONG_LONG_MAX__
#endif

/* global options */
static char /*@only@*/ /*@notnull@*/ // *g*
	*mirrordir = NULL ,
	*distdir = NULL,
	*dbdir = NULL,
	*listdir = NULL,
	*confdir = NULL,
	*logdir = NULL,
	/* This should have never been a seperate directory, well to late... */
	*overridedir = NULL,
	*methoddir = NULL;
static char /*@only@*/ /*@null@*/
	*section = NULL,
	*priority = NULL,
	*component = NULL,
	*architecture = NULL,
	*packagetype = NULL;
static int	delete = D_COPY;
static bool	nothingiserror = false;
static bool	nolistsdownload = false;
static bool	keepunreferenced = false;
static bool	keepunneededlists = false;
static bool	keepdirectories = false;
static bool	askforpassphrase = false;
static bool	guessgpgtty = true;
static bool	skipold = true;
static size_t   waitforlock = 0;
static enum exportwhen export = EXPORT_NORMAL;
int		verbose = 0;
static bool	fast = false;
static enum spacecheckmode spacecheckmode = scm_FULL;
/* default: 100 MB for database to grow */
static off_t reserveddbspace = 1024*1024*100
/* 1MB safety margin for other fileystems */;
static off_t reservedotherspace = 1024*1024;

/* define for each config value an owner, and only higher owners are allowed
 * to change something owned by lower owners. */
enum config_option_owner config_state,
#define O(x) owner_ ## x = CONFIG_OWNER_DEFAULT
O(fast), O(mirrordir), O(distdir), O(dbdir), O(listdir), O(confdir), O(logdir), O(overridedir), O(methoddir), O(section), O(priority), O(component), O(architecture), O(packagetype), O(nothingiserror), O(nolistsdownload), O(keepunreferenced), O(keepunneededlists), O(keepdirectories), O(askforpassphrase), O(skipold), O(export), O(waitforlock), O(spacecheckmode), O(reserveddbspace), O(reservedotherspace), O(guessgpgtty);
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

static inline retvalue removeunreferencedfiles(struct database *database,struct strlist *dereferencedfilekeys) {
	int i;
	retvalue result,r;
	result = RET_OK;

	for( i = 0 ; i < dereferencedfilekeys->count ; i++ ) {
		const char *filekey = dereferencedfilekeys->values[i];

		r = references_isused(database,filekey);
		if( r == RET_NOTHING ) {
			r = files_deleteandremove(database, filekey,
					!keepdirectories, true);
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
			UNUSED(struct distribution *dummy2), \
			UNUSED(struct database *dummy),	\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_C(name) static retvalue action_c_ ## name ( \
			struct distribution *alldistributions, \
			UNUSED(struct database *dummy),	\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_u_B(name) static retvalue action_b_ ## name ( \
			UNUSED(struct distribution *ddummy), \
			struct database *database,	\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_B(name) static retvalue action_b_ ## name ( \
			struct distribution *alldistributions, \
			struct database *database,	\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_U_R(name) static retvalue action_r_ ## name ( \
			UNUSED(struct distribution *ddummy), \
			struct database *database,		\
			UNUSED(struct strlist* dummy3), \
			UNUSED(int argc), UNUSED(const char *dummy4[]))

#define ACTION_u_R(name) static retvalue action_r_ ## name ( \
			UNUSED(struct distribution *ddummy), \
			struct database *database,		\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_R(name) static retvalue action_r_ ## name ( \
			struct distribution *alldistributions, \
			struct database *database,		\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_U_F(name) static retvalue action_f_ ## name ( \
			UNUSED(struct distribution *ddummy), \
			struct database *database,		\
			UNUSED(struct strlist* dummy3), \
			UNUSED(int argc), UNUSED(const char *dummy4[]))

#define ACTION_u_F(name) static retvalue action_f_ ## name ( \
			UNUSED(struct distribution *ddummy), \
			struct database *database,		\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])
#define ACTION_F(name) static retvalue action_f_ ## name ( \
			struct distribution *alldistributions, \
			struct database *database,		\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_RF(name) static retvalue action_rf_ ## name ( \
			struct distribution *alldistributions, \
			struct database *database,		\
			UNUSED(struct strlist* dummy3), \
			int argc,const char *argv[])

#define ACTION_U_RF(name) static retvalue action_rf_ ## name ( \
			UNUSED(struct distribution *ddummy), \
			struct database *database,		\
			UNUSED(struct strlist* dummy3), \
			UNUSED(int argc) ,UNUSED(const char *dumym4[]))

#define ACTION_D(name) static retvalue action_d_ ## name ( \
			struct distribution *alldistributions, \
			struct database *database,		\
			struct strlist* dereferenced, 	\
			int argc,const char *argv[])

#define ACTION_U_D(name) static retvalue action_d_ ## name ( \
			struct distribution *alldistributions, \
			struct database *database,		\
			struct strlist* dereferenced, 	\
			UNUSED(int argc), UNUSED(const char *dummy4[]))

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

	assert( argc == 2 );

	result = extractcontrol(&control,argv[1]);

	if( RET_IS_OK(result) )
		printf("%s\n",control);
	return result;
}
ACTION_N(extractfilelist) {
	retvalue result;
	char *filelist;

	assert( argc == 2 );

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

ACTION_u_F(fakeemptyfilelist) {
	assert( argc == 2 );
	return files_addfilelist(database, argv[1], "");
}


ACTION_u_F(generatefilelists) {
	assert( argc == 2 || argc == 3 );
	if( argc == 2 && strcmp(argv[1],"reread") != 0 ) {
		fprintf(stderr,"Error: Unrecognized second argument '%s'\n"
				"Syntax: reprepro generatefilelists [reread]\n",
				argv[1]);
		return RET_ERROR;
	}
	return files_regenerate_filelist(database, argc == 2);
}


ACTION_U_F(addmd5sums) {
	char buffer[2000],*c,*m;
	retvalue result,r;

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
		r = files_add(database,buffer,m);
		RET_UPDATE(result,r);

	}
	return result;
}


ACTION_u_R(removereferences) {
	assert( argc == 2 );
	return references_remove(database, argv[1], NULL);
}


ACTION_U_R(dumpreferences) {
	return references_dump(database);
}

static retvalue checkifreferenced(void *data,const char *filekey,UNUSED(const char *md5sum)) {
	struct database *database = data;
	retvalue r;

	r = references_isused(database, filekey);
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

	result = files_foreach(database, checkifreferenced, database);
	return result;
}

static retvalue deleteifunreferenced(void *data,const char *filekey,UNUSED(const char *md5sum)) {
	struct database *database = data;
	retvalue r;

	r = references_isused(database,filekey);
	if( r == RET_NOTHING ) {
		r = files_deleteandremove(database, filekey,
				!keepdirectories, false);
		return r;
	} else if( RET_IS_OK(r) ) {
		return RET_NOTHING;
	} else
		return r;
}

ACTION_U_RF(deleteunreferenced) {
	retvalue result;

	if( keepunreferenced ) {
		fprintf(stderr,"Calling deleteunreferenced with --keepunreferencedfiles does not really make sense, does it?\n");
		return RET_ERROR;
	}
	result = files_foreach(database, deleteifunreferenced, database);
	return result;
}

ACTION_u_R(addreference) {
	assert( argc == 2 || argc == 3 );
	return references_increment(database, argv[1], argv[2]);
}


struct remove_args {/*@temp@*/struct database *db; int count; /*@temp@*/ const char * const *names; bool *gotremoved; int todo;/*@temp@*/struct strlist *removedfiles;/*@temp@*/struct trackingdata *trackingdata;struct logger *logger;};

static retvalue remove_from_target(/*@temp@*/void *data, struct target *target,
		UNUSED(struct distribution *dummy)) {
	retvalue result,r;
	int i;
	struct remove_args *d = data;

	result = RET_NOTHING;
	for( i = 0 ; i < d->count ; i++ ){
		r = target_removepackage(target, d->logger, d->db,
				d->names[i], NULL,
				d->removedfiles, d->trackingdata);
		if( RET_IS_OK(r) ) {
			if( ! d->gotremoved[i] )
				d->todo--;
			d->gotremoved[i] = true;
		}
		RET_UPDATE(result,r);
	}
	return result;
}

ACTION_D(remove) {
	retvalue result,r;
	struct distribution *distribution;
	struct remove_args d;
	trackingdb tracks;
	struct trackingdata trackingdata;

	r = distribution_get(alldistributions, argv[1], true, &distribution);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;

	r = distribution_prepareforwriting(distribution);
	if( RET_WAS_ERROR(r) )
		return r;

	if( distribution->tracking != dt_NONE ) {
		r = tracking_initialize(&tracks, database, distribution);
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
		r = trackingdata_new(tracks,&trackingdata);
		if( RET_WAS_ERROR(r) ) {
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
	d.db = database;
	d.removedfiles = dereferenced;
	d.logger = distribution->logger;
	if( d.gotremoved == NULL )
		result = RET_ERROR_OOM;
	else
		result = distribution_foreach_rwopenedpart(distribution, database,
				component, architecture, packagetype,
				remove_from_target, &d);
	d.db = NULL;
	d.removedfiles = NULL;

	logger_wait();

	r = distribution_export(export, distribution, confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	if( d.trackingdata != NULL ) {
		if( RET_WAS_ERROR(result) )
			trackingdata_done(d.trackingdata);
		else
			trackingdata_finish(tracks, d.trackingdata, database, dereferenced);
		r = tracking_done(tracks);
		RET_ENDUPDATE(result,r);
	}
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

	result = packages_get(target->packages, packagename, &control);
	if( RET_IS_OK(result) ) {
		r = (*target->getversion)(target, control, &version);
		if( RET_IS_OK(r) ) {
			printf("%s: %s %s\n",target->identifier,packagename,version);
			free(version);
		} else {
			printf("Could not retrieve version from %s in %s\n",packagename,target->identifier);
		}
		free(control);
	}
	return result;
}

ACTION_B(list) {
	retvalue r,result;
	struct distribution *distribution;

	assert( argc == 3 );

	r = distribution_get(alldistributions, argv[1], false, &distribution);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;

	result = distribution_foreach_roopenedpart(distribution, database,
			component, architecture, packagetype,
			list_in_target, (void*)argv[2]);
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
	retvalue result;
	/*@temp@*/ struct listfilter d;

	d.target = target;
	d.condition = data;
	result = packages_foreach(target->packages,listfilterprint,&d);
	d.target = NULL;
	d.condition = NULL;

	return result;
}

ACTION_B(listfilter) {
	retvalue r,result;
	struct distribution *distribution;
	term *condition;

	assert( argc == 3 );

	r = distribution_get(alldistributions, argv[1], false, &distribution);
	assert( r != RET_NOTHING);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	result = term_compile(&condition,argv[2],T_OR|T_BRACKETS|T_NEGATION|T_VERSION|T_NOTEQUAL);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_foreach_roopenedpart(distribution, database, component, architecture, packagetype, listfilter_in_target, condition);
	term_free(condition);
	return result;
}

ACTION_u_F(detect) {
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	ret = RET_NOTHING;
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			r = files_detect(database,argv[i]);
			RET_UPDATE(ret,r);
		}

	} else
		while( fgets(buffer,4999,stdin) != NULL ) {
			nl = strchr(buffer,'\n');
			if( nl == NULL ) {
				return RET_ERROR;
			}
			*nl = '\0';
			r = files_detect(database,buffer);
			RET_UPDATE(ret,r);
		}
	return ret;
}

ACTION_u_F(forget) {
	char buffer[5000],*nl;
	int i;
	retvalue r,ret;

	ret = RET_NOTHING;
	if( argc > 1 ) {
		for( i = 1 ; i < argc ; i++ ) {
			r = files_remove(database, argv[i], false);
			RET_UPDATE(ret,r);
		}

	} else
		while( fgets(buffer,4999,stdin) != NULL ) {
			nl = strchr(buffer,'\n');
			if( nl == NULL ) {
				return RET_ERROR;
			}
			*nl = '\0';
			r = files_remove(database, buffer, false);
			RET_UPDATE(ret,r);
		}
	return ret;
}

ACTION_U_F(listmd5sums) {
	return files_printmd5sums(database);
}

static retvalue printout(UNUSED(void *data),const char *package,const char *chunk){
	printf("'%s' -> '%s'\n",package,chunk);
	return RET_OK;
}

ACTION_u_B(dumpcontents) {
	retvalue result,r;
	packagesdb packages;

	assert( argc == 2 );

	result = packages_initialize(&packages, database, argv[1]);
	if( RET_WAS_ERROR(result) )
		return result;

	result = packages_foreach(packages,printout,NULL);

	r = packages_done(packages);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_F(export) {
	retvalue result,r;
	struct distribution *d;

	if( export == EXPORT_NEVER ) {
		fprintf(stderr, "Error: reprepro export incompatible with --export=never\n");
		return RET_ERROR;
	}

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING);
	if( RET_WAS_ERROR(result) )
		return result;
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;

		if( verbose > 0 ) {
			printf("Exporting %s...\n",d->codename);
		}

		r = distribution_fullexport(d, confdir, distdir, database);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && export != EXPORT_FORCE) {
			return r;
		}
	}
	return result;
}

/***********************update********************************/

ACTION_D(update) {
	retvalue result,r;
	struct update_pattern *patterns;
	struct update_distribution *u_distributions;

	result = dirs_make_recursive(listdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_getpatterns(confdir, &patterns);
	if( RET_WAS_ERROR(result) )
		return result;
	assert( RET_IS_OK(result) );

	result = updates_calcindices(listdir, patterns, alldistributions, &u_distributions);
	if( RET_WAS_ERROR(result) ) {
		updates_freepatterns(patterns);
		return result;
	}
	assert( RET_IS_OK(result) );

	if( !keepunneededlists ) {
		result = updates_clearlists(listdir,u_distributions);
	}
	if( !RET_WAS_ERROR(result) )
		result = updates_update(database, methoddir, u_distributions,
				nolistsdownload, skipold, dereferenced,
				spacecheckmode, reserveddbspace, reservedotherspace);
	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);

	r = distribution_exportlist(export, alldistributions,
			confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_D(predelete) {
	retvalue result,r;
	struct update_pattern *patterns;
	struct update_distribution *u_distributions;

	result = dirs_make_recursive(listdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_getpatterns(confdir,&patterns);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	assert( RET_IS_OK(result) );

	result = updates_calcindices(listdir, patterns, alldistributions, &u_distributions);
	if( RET_WAS_ERROR(result) ) {
		updates_freepatterns(patterns);
		return result;
	}
	assert( RET_IS_OK(result) );

	if( !keepunneededlists ) {
		result = updates_clearlists(listdir, u_distributions);
	}
	if( !RET_WAS_ERROR(result) )
		result = updates_predelete(database, methoddir, u_distributions, nolistsdownload, skipold, dereferenced);
	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);

	r = distribution_exportlist(export, alldistributions,
			confdir, distdir, database);
	RET_ENDUPDATE(result, r);

	return result;
}

ACTION_D(iteratedupdate) {
	retvalue result;
	struct update_pattern *patterns;
	struct update_distribution *u_distributions;

	result = dirs_make_recursive(listdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_getpatterns(confdir, &patterns);
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_calcindices(listdir, patterns, alldistributions, &u_distributions);
	if( RET_WAS_ERROR(result) ) {
		updates_freepatterns(patterns);
		return result;
	}

	if( !keepunneededlists ) {
		result = updates_clearlists(listdir,u_distributions);
	}
	if( !RET_WAS_ERROR(result) )
		result = updates_iteratedupdate(confdir, database, distdir, methoddir, u_distributions, nolistsdownload, skipold, dereferenced, export, spacecheckmode, reserveddbspace, reservedotherspace);
	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);

	return result;
}

ACTION_B(checkupdate) {
	retvalue result;
	struct update_pattern *patterns;
	struct update_distribution *u_distributions;

	result = dirs_make_recursive(listdir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = distribution_match(alldistributions, argc-1, argv+1, false);
	assert( result != RET_NOTHING);
	if( RET_WAS_ERROR(result) )
		return result;

	result = updates_getpatterns(confdir, &patterns);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	result = updates_calcindices(listdir, patterns, alldistributions, &u_distributions);
	if( RET_WAS_ERROR(result) ) {
		updates_freepatterns(patterns);
		return result;
	}

	result = updates_checkupdate(database, methoddir, u_distributions,
			nolistsdownload, skipold);

	updates_freeupdatedistributions(u_distributions);
	updates_freepatterns(patterns);

	return result;
}
/***********************migrate*******************************/

ACTION_D(pull) {
	retvalue result,r;
	struct pull_rule *rules;
	struct pull_distribution *p;

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = pull_getrules(confdir, &rules);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	assert( RET_IS_OK(result) );

	result = pull_prepare(alldistributions, rules, fast, &p);
	if( RET_WAS_ERROR(result) ) {
		pull_freerules(rules);
		return result;
	}
	result = pull_update(database, p, dereferenced);

	pull_freerules(rules);
	pull_freedistributions(p);

	r = distribution_exportlist(export, alldistributions,
			confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_B(checkpull) {
	retvalue result;
	struct pull_rule *rules;
	struct pull_distribution *p;

	result = distribution_match(alldistributions, argc-1, argv+1, false);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = pull_getrules(confdir,&rules);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	assert( RET_IS_OK(result) );

	result = pull_prepare(alldistributions, rules, fast, &p);
	if( RET_WAS_ERROR(result) ) {
		pull_freerules(rules);
		return result;
	}
	result = pull_checkupdate(database, p);

	pull_freerules(rules);
	pull_freedistributions(p);

	return result;
}

struct copy_data {
	struct distribution *destination;
	/*@temp@*/struct database *db;
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

	result = target_initpackagesdb(origtarget, d->db);
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

	result = target_initpackagesdb(dsttarget, d->db);
	if( RET_WAS_ERROR(result) ) {
		RET_UPDATE(d->destination->status,result);
		free(chunk);
		free(version);
		strlist_done(&filekeys);
		return result;
	}

	assert( logger_isprepared(d->destination->logger) );

	result = target_addpackage(dsttarget, d->destination->logger,
			d->db, d->name, version, chunk,
			&filekeys, true, d->removedfiles,
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

	result = distribution_get(alldistributions, argv[1], true, &destination);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;
	result = distribution_get(alldistributions, argv[2], false, &source);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = distribution_prepareforwriting(destination);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	if( destination->tracking != dt_NONE ) {
		fprintf(stderr, "WARNING: copy does not yet support trackingdata and will ignore trackingdata in '%s'!\n", destination->codename);
	}

	d.destination = destination;
	d.db = database;
	d.removedfiles = dereferenced;
	for( i = 3; i < argc ; i++ ) {
		d.name = argv[i];
		if( verbose > 0 )
			printf("Looking for '%s' in '%s' to be copied to '%s'...\n",
					d.name, source->codename,
					destination->codename);
		result = distribution_foreach_part(source,component,architecture,packagetype,copy,&d);
	}
	logger_wait();

	d.db = NULL;
	d.removedfiles = NULL;
	d.destination = NULL;

	r = distribution_export(export, destination, confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	return result;

}

/***********************rereferencing*************************/
static retvalue reref(void *data,struct target *target,UNUSED(struct distribution *di)) {
	struct database *database = data;

	return target_rereference(target, database);
}


ACTION_R(rereference) {
	retvalue result,r;
	struct distribution *d;

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;

		if( verbose > 0 ) {
			printf("Referencing %s...\n",d->codename);
		}

		r = distribution_foreach_roopenedpart(d, database,
				component, architecture, packagetype,
				reref, database);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}

	return result;
}
/***************************retrack****************************/
struct data_binsrctrack { /*@temp@*/struct database *db; trackingdb tracks;};

static retvalue retrack(void *data,struct target *target,UNUSED(struct distribution *di)) {
	struct data_binsrctrack *d = data;

	return target_retrack(target, d->tracks, d->db);
}

ACTION_R(retrack) {
	retvalue result,r;
	struct distribution *d;

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		struct data_binsrctrack dat;

		if( !d->selected )
			continue;
		if( verbose > 0 ) {
			fprintf(stderr,"Chasing %s...\n",d->codename);
		}
		dat.db = database;
		r = tracking_initialize(&dat.tracks, database, d);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
			continue;
		}
		r = tracking_removeall(dat.tracks);
		RET_UPDATE(result,r);
		r = references_remove(database, d->codename, NULL);
		RET_UPDATE(result,r);

		r = distribution_foreach_roopenedpart(d, database,
				component, architecture, packagetype,
				retrack, &dat);
		RET_UPDATE(result,r);
		dat.db = NULL;
		r = tracking_done(dat.tracks);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(result) )
			break;
	}
	return result;
}

ACTION_D(removetrack) {
	retvalue result,r;
	struct distribution *distribution;
	trackingdb tracks;

	assert( argc == 4 );

	result = distribution_get(alldistributions, argv[1], true, &distribution);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;
	r = tracking_initialize(&tracks, database, distribution);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}

	result = tracking_remove(tracks, argv[2], argv[3], database, dereferenced);

	r = tracking_done(tracks);
	RET_ENDUPDATE(result,r);
	return result;
}

ACTION_D(removealltracks) {
	retvalue result,r;
	struct distribution *d;

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		trackingdb tracks;

		if( !d->selected )
			continue;

		if( verbose >= 0 ) {
			printf("Deleting all tracks for %s...\n",d->codename);
		}
		r = tracking_initialize(&tracks, database, d);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
			continue;
		}
		r = tracking_removeall(tracks);
		RET_UPDATE(result,r);
		r = references_remove(database, d->codename, dereferenced);
		RET_UPDATE(result,r);
		r = tracking_done(tracks);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(result) )
			break;
	}
	return result;
}

ACTION_D(tidytracks) {
	retvalue result,r;
	struct distribution *d;

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		trackingdb tracks;

		if( !d->selected )
			continue;

		if( verbose >= 0 ) {
			printf("Looking for old tracks in %s...\n",d->codename);
		}
		r = tracking_initialize(&tracks, database, d);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
			continue;
		}
		r = tracking_tidyall(tracks, database, dereferenced);
		RET_UPDATE(result,r);
		r = tracking_done(tracks);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(result) )
			break;
	}
	return result;
}

ACTION_B(dumptracks) {
	retvalue result,r;
	struct distribution *d;

	result = distribution_match(alldistributions, argc-1, argv+1, false);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		trackingdb tracks;

		if( !d->selected )
			continue;

		r = tracking_initialize(&tracks, database, d);
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
	return result;
}
/***********************checking*************************/

static retvalue check_target(void *data,struct target *target,UNUSED(struct distribution *di)) {
	return target_check(target, data);
}

ACTION_RF(check) {
	retvalue result,r;
	struct distribution *d;

	result = distribution_match(alldistributions, argc-1, argv+1, false);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;

		if( verbose > 0 ) {
			printf("Checking %s...\n",d->codename);
		}

		r = distribution_foreach_roopenedpart(d, database,
				component, architecture, packagetype,
				check_target, database);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	return result;
}

ACTION_u_F(checkpool) {

	if( argc == 2 && strcmp(argv[1],"fast") != 0 ) {
		fprintf(stderr,"Error: Unrecognized second argument '%s'\n"
				"Syntax: reprepro checkpool [fast]\n",
				argv[1]);
		return RET_ERROR;
	}

	return files_checkpool(database, argc == 2);
}
/*****************reapplying override info***************/

static retvalue reoverride_target(UNUSED(void *data),struct target *target,struct distribution *distribution) {

	return target_reoverride(target, distribution);
}

ACTION_F(reoverride) {
	retvalue result,r;
	struct distribution *d;

	result = distribution_match(alldistributions, argc-1, argv+1, true);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {

		if( !d->selected )
			continue;

		if( verbose > 0 ) {
			fprintf(stderr,"Reapplying override to %s...\n",d->codename);
		}

		r = distribution_loadalloverrides(d, confdir, overridedir);
		if( RET_IS_OK(r) ) {
			r = distribution_foreach_rwopenedpart(d, database,
					component, architecture, packagetype,
					reoverride_target, NULL);
			distribution_unloadoverrides(d);
		} else if( r == RET_NOTHING ) {
			fprintf(stderr,"No override files, thus nothing to do for %s.\n",d->codename);
		}
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	r = distribution_exportlist(export, alldistributions, confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	return result;
}

/***********************include******************************************/

ACTION_D(includedeb) {
	retvalue result,r;
	struct distribution *distribution;
	bool isudeb;
	trackingdb tracks;
	int i = 0;

	if( architecture != NULL && strcmp(architecture,"source") == 0 ) {
		fprintf(stderr,"Error: -A source is not possible with includedeb!\n");
		return RET_ERROR;
	}
	if( strcmp(argv[0],"includeudeb") == 0 ) {
		isudeb = true;
		if( packagetype != NULL && strcmp(packagetype,"udeb") != 0 ) {
			fprintf(stderr,"Calling includeudeb with a -T different from 'udeb' makes no sense!\n");
			return RET_ERROR;
		}
	} else if( strcmp(argv[0],"includedeb") == 0 ) {
		isudeb = false;
		if( packagetype != NULL && strcmp(packagetype,"deb") != 0 ) {
			fprintf(stderr,"Calling includedeb with -T something where something is not 'deb' makes no sense!\n");
			return RET_ERROR;
		}

	} else {
		fprintf(stderr,"Internal error while parding command!\n");
		return RET_ERROR;
	}

	for( i = 2 ; i < argc ; i++ ) {
		const char *filename = argv[i];

		if( isudeb ) {
			if( !endswith(filename,".udeb") && !IGNORING_(extension,
"includeudeb called with file '%s' not ending with '.udeb'\n", filename) )
				return RET_ERROR;
		} else {
			if( !endswith(filename,".deb") && !IGNORING_(extension,
"includedeb called with file '%s' not ending with '.deb'\n", filename) )
				return RET_ERROR;
		}
	}

	result = distribution_get(alldistributions, argv[1], true, &distribution);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	if( isudeb )
		result = override_read(confdir, overridedir,
				distribution->udeb_override,
				&distribution->overrides.udeb);
	else
		result = override_read(confdir, overridedir,
				distribution->deb_override,
				&distribution->overrides.deb);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	// TODO: same for component? (depending on type?)
	if( architecture != NULL && !strlist_in(&distribution->architectures,architecture) ){
		fprintf(stderr,"Cannot force into the architecture '%s' not available in '%s'!\n",architecture,distribution->codename);
		return RET_ERROR;
	}

	r = distribution_prepareforwriting(distribution);
	if( RET_WAS_ERROR(r) ) {
		return RET_ERROR;
	}

	if( distribution->tracking != dt_NONE ) {
		result = tracking_initialize(&tracks, database, distribution);
		if( RET_WAS_ERROR(result) ) {
			return result;
		}
	} else {
		tracks = NULL;
	}
	result = RET_NOTHING;
	for( i = 2 ; i < argc ; i++ ) {
		const char *filename = argv[i];

		r = deb_add(database, component, architecture,
				section,priority,isudeb?"udeb":"deb",distribution,filename,
				NULL,NULL,delete,
				dereferenced,tracks);
		RET_UPDATE(result, r);
	}

	distribution_unloadoverrides(distribution);

	r = tracking_done(tracks);
	RET_ENDUPDATE(result,r);

	logger_wait();

	r = distribution_export(export, distribution, confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	return result;
}


ACTION_D(includedsc) {
	retvalue result,r;
	struct distribution *distribution;
	trackingdb tracks;

	assert( argc == 3 );

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

	result = distribution_get(alldistributions, argv[1], true, &distribution);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;
	result = override_read(confdir, overridedir,
			distribution->dsc_override,
			&distribution->overrides.dsc);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = distribution_prepareforwriting(distribution);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	if( distribution->tracking != dt_NONE ) {
		result = tracking_initialize(&tracks, database, distribution);
		if( RET_WAS_ERROR(result) ) {
			return result;
		}
	} else {
		tracks = NULL;
	}

	result = dsc_add(database, component, section, priority,
			distribution, argv[2], delete,
			dereferenced, tracks);
	logger_wait();

	distribution_unloadoverrides(distribution);
	r = tracking_done(tracks);
	RET_ENDUPDATE(result,r);
	r = distribution_export(export, distribution,
			confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	return result;
}

ACTION_D(include) {
	retvalue result,r;
	struct distribution *distribution;
	trackingdb tracks;

	assert( argc == 3 );

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

	result = distribution_get(alldistributions, argv[1], true, &distribution);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = distribution_loadalloverrides(distribution, confdir, overridedir);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	if( distribution->tracking != dt_NONE ) {
		result = tracking_initialize(&tracks, database, distribution);
		if( RET_WAS_ERROR(result) ) {
			return result;
		}
	} else {
		tracks = NULL;
	}
	result = distribution_loaduploaders(distribution, confdir);
	if( RET_WAS_ERROR(result) ) {
		r = tracking_done(tracks);
		RET_ENDUPDATE(result,r);
		return result;
	}
	result = changes_add(database, tracks,
			packagetype, component, architecture,
			section, priority, distribution,
			argv[2], delete, dereferenced);

	distribution_unloadoverrides(distribution);
	distribution_unloaduploaders(distribution);
	r = tracking_done(tracks);
	RET_ENDUPDATE(result,r);
	r = distribution_export(export,distribution,confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	return result;
}

/***********************createsymlinks***********************************/

ACTION_C(createsymlinks) {
	retvalue result,r;
	struct distribution *d,*d2;

	r = dirs_make_recursive(distdir);
	if( RET_WAS_ERROR(r) )
		return r;

	result = distribution_match(alldistributions, argc-1, argv+1, false);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) ) {
		return result;
	}
	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		char *linkname,*buffer;
		size_t bufsize;
		int ret;

		if( !d->selected )
			continue;

		if( d->suite == NULL || strcmp(d->suite, d->codename) == 0 )
			continue;
		r = RET_NOTHING;
		for( d2 = alldistributions ; d2 != NULL ; d2 = d2->next ) {
			if( !d2->selected )
				continue;
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
	return result;
}

/***********************clearvanished***********************************/

static retvalue docount(void *data,UNUSED(const char *a),UNUSED(const char *b)) {
	long int *p = data;

	(*p)++;
	return RET_OK;
}


ACTION_U_D(clearvanished) {
	retvalue result,r;
	struct distribution *d;
	struct strlist identifiers;
	bool *inuse;
	int i;

	result = packages_getdatabases(database, &identifiers);
	if( RET_WAS_ERROR(result) ) {
		return result;
	}

	inuse = calloc(identifiers.count, sizeof(bool));
	if( inuse == NULL ) {
		strlist_done(&identifiers);
		return RET_ERROR_OOM;
	}
	for( d = alldistributions; d != NULL ; d = d->next ) {
		struct target *t;
		for( t = d->targets; t != NULL ; t = t->next ) {
			int i = strlist_ofs(&identifiers, t->identifier);
			if( i >= 0 ) {
				inuse[i] = true;
				if( verbose > 6 )
					printf(
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
			r = packages_initialize(&db, database, identifier);
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
		printf(
"Deleting vanished identifier '%s'.\n", identifier);
		/* derference anything left */
		references_remove(database, identifier, dereferenced);
		/* remove the database */
		packages_drop(database, identifier);
	}
	free(inuse);

	strlist_done(&identifiers);
	return result;
}

ACTION_N(versioncompare) {
	retvalue r;
	int i;

	assert( argc == 3 );

	r = properversion(argv[1]);
	if( RET_WAS_ERROR(r) )
		fprintf(stderr, "'%s' is not a proper version!\n", argv[1]);
	r = properversion(argv[2]);
	if( RET_WAS_ERROR(r) )
		fprintf(stderr, "'%s' is not a proper version!\n", argv[2]);
	r = dpkgversions_cmp(argv[1],argv[2],&i);
	if( RET_IS_OK(r) ) {
		if( i < 0 ) {
			printf("'%s' is smaller than '%s'.\n",
						argv[1], argv[2]);
		} else if( i > 0 ) {
			printf("'%s' is larger than '%s'.\n",
					argv[1], argv[2]);
		} else
			printf("'%s' is the same as '%s'.\n",
					argv[1], argv[2]);
	}
	return r;
}
/***********************processincoming********************************/
ACTION_D(processincoming) {
	retvalue result,r;
	struct distribution *d;

	for( d = alldistributions ; d != NULL ; d = d->next )
		d->selected = true;

	result = process_incoming(mirrordir, confdir, overridedir, database, dereferenced, alldistributions, argv[1], (argc==3)?argv[2]:NULL);

	logger_wait();

	r = distribution_exportlist(export, alldistributions,
			confdir, distdir, database);
	RET_ENDUPDATE(result,r);

	return result;
}
/***********************gensnapshot********************************/
ACTION_R(gensnapshot) {
	retvalue result;
	struct distribution *distribution;

	assert( argc == 3 );

	result = distribution_get(alldistributions, argv[1], true, &distribution);
	assert( result != RET_NOTHING );
	if( RET_WAS_ERROR(result) )
		return result;

	result = distribution_snapshot(distribution,
	                               confdir, distdir,
				       database,
	                               argv[2]);
	return result;
}


/***********************rerunnotifiers********************************/
static retvalue runnotifiers(UNUSED(void *data),struct target *target,struct distribution *d) {
	if( !logger_rerun_needs_target(d->logger, target) )
		return RET_NOTHING;

	return target_rerunnotifiers(target, d->logger);
}

ACTION_B(rerunnotifiers) {
	retvalue result,r;
	struct distribution *d;

	result = distribution_match(alldistributions, argc-1, argv+1, false);
	assert( result != RET_NOTHING);
	if( RET_WAS_ERROR(result) )
		return result;

	result = RET_NOTHING;
	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;

		if( d->logger == NULL )
			continue;

		if( verbose > 0 ) {
			printf("Processing %s...\n",d->codename);
		}
		r = logger_prepare(d->logger);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;

		r = distribution_foreach_roopenedpart(d, database,
				component, architecture, packagetype,
				runnotifiers, NULL);
		logger_wait();

		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	return result;


}

/*********************/
/* argument handling */
/*********************/

#define NEED_REFERENCES 1
#define NEED_FILESDB 2
#define NEED_DEREF 4
#define NEED_DATABASE 8
#define NEED_CONFIG 16
#define NEED_NO_PACKAGES 32
#define IS_RO 64
#define IS_CLEARVANISHED 128
#define A_N(w) action_n_ ## w, 0
#define A_C(w) action_c_ ## w, NEED_CONFIG
#define A_ROB(w) action_b_ ## w, NEED_DATABASE|IS_RO
#define A_B(w) action_b_ ## w, NEED_DATABASE
#define A_F(w) action_f_ ## w, NEED_DATABASE|NEED_FILESDB
#define A_R(w) action_r_ ## w, NEED_DATABASE|NEED_REFERENCES
#define A__F(w) action_f_ ## w, NEED_DATABASE|NEED_FILESDB|NEED_NO_PACKAGES
#define A__R(w) action_r_ ## w, NEED_DATABASE|NEED_REFERENCES|NEED_NO_PACKAGES
#define A_RF(w) action_rf_ ## w, NEED_DATABASE|NEED_FILESDB|NEED_REFERENCES
/* to dereference files, one needs files and references database: */
#define A_D(w) action_d_ ## w, NEED_DATABASE|NEED_FILESDB|NEED_REFERENCES|NEED_DEREF

static const struct action {
	const char *name;
	retvalue (*start)(
			/*@null@*//*@only@*/struct distribution *,
			/*@null@*/struct database*,
			/*@null@*/struct strlist *dereferencedfilekeys,
			int argc,const char *argv[]);
	int needs;
	int minargs, maxargs;
	const char *wrongargmessage;
} all_actions[] = {
	{"__d", 		A_N(printargs),
		-1, -1, NULL},
	{"__extractcontrol",	A_N(extractcontrol),
		1, 1, "__extractcontrol <.deb-file>"},
	{"__extractfilelist",	A_N(extractfilelist),
		1, 1, "__extractfilelist <.deb-file>"},
	{"_versioncompare",	A_N(versioncompare),
		2, 2, "versioncompare <version> <version>"},
	{"_detect", 		A__F(detect),
		-1, -1, NULL},
	{"_forget", 		A__F(forget),
		-1, -1, NULL},
	{"_listmd5sums",	A__F(listmd5sums),
		0, 0, "_listmd5sums "},
	{"_addmd5sums",		A__F(addmd5sums),
		0, 0, "_addmd5sums < data"},
	{"_dumpcontents", 	A_ROB(dumpcontents),
		1, 1, "_dumpcontents <identifier>"},
	{"_removereferences", 	A__R(removereferences),
		1, 1, "_removereferences <identifier>"},
	{"_addreference", 	A__R(addreference),
		2, 2, "_addreference <reference> <referee>"},
	{"_fakeemptyfilelist",	A__F(fakeemptyfilelist),
		1, 1, "_fakeemptyfilelist <filekey>"},
	{"remove", 		A_D(remove),
		2, -1, "[-C <component>] [-A <architecture>] [-T <type>] remove <codename> <package-names>"},
	{"list", 		A_ROB(list),
		2, -1, "[-C <component>] [-A <architecture>] [-T <type>] list <codename> <package-name>"},
	{"listfilter", 		A_ROB(listfilter),
		2, 2, "[-C <component>] [-A <architecture>] [-T <type>] listfilter <codename> <term to describe which packages to list>"},
	{"createsymlinks", 	A_C(createsymlinks),
		0, -1, "createsymlinks [<distributions>]"},
	{"export", 		A_F(export),
		0, -1, "export [<distributions>]"},
	{"check", 		A_RF(check),
		0, -1, "check [<distributions>]"},
	{"reoverride", 		A_F(reoverride),
		0, -1, "[-T ...] [-C ...] [-A ...] reoverride [<distributions>]"},
	{"checkpool", 		A_F(checkpool),
		0, 1, "checkpool [fast]"},
	{"rereference", 	A_R(rereference),
		0, -1, "rereference [<distributions>]"},
	{"dumpreferences", 	A_R(dumpreferences),
		0, 0, "dumpreferences", },
	{"dumpunreferenced", 	A_RF(dumpunreferenced),
		0, 0, "dumpunreferenced", },
	{"deleteunreferenced", 	A_RF(deleteunreferenced),
		0, 0, "deleteunreferenced", },
	{"retrack",	 	A_R(retrack),
		0, -1, "retrack [<distributions>]"},
	{"dumptracks",	 	A_ROB(dumptracks),
		0, -1, "dumptracks [<distributions>]"},
	{"removealltracks",	A_D(removealltracks),
		0, -1, "removealltracks [<distributions>]"},
	{"tidytracks",		A_D(tidytracks),
		0, -1, "tidytracks [<distributions>]"},
	{"removetrack",		A_D(removetrack),
		3, 3, "removetrack <distribution> <sourcename> <version>"},
	{"update",		A_D(update),
		0, -1, "update [<distributions>]"},
	{"iteratedupdate",	A_D(iteratedupdate),
		0, -1, "iteratedupdate [<distributions>]"},
	{"checkupdate",		A_B(checkupdate),
		0, -1, "checkupdate [<distributions>]"},
	{"predelete",		A_D(predelete),
		0, -1, "predelete [<distributions>]"},
	{"pull",		A_D(pull),
		0, -1, "pull [<distributions>]"},
	{"copy",		A_D(copy),
		3, -1, "[-C <component> ] [-A <architecture>] [-T <packagetype>] copy <destination-distribution> <source-distribution> <package-names to pull>"},
	{"checkpull",		A_B(checkpull),
		0, -1, "checkpull [<distributions>]"},
	{"includedeb",		A_D(includedeb),
		2, -1, "[--delete] includedeb <distribution> <.deb-file>"},
	{"includeudeb",		A_D(includedeb),
		2, -1, "[--delete] includeudeb <distribution> <.udeb-file>"},
	{"includedsc",		A_D(includedsc),
		2, 2, "[--delete] includedsc <distribution> <package>"},
	{"include",		A_D(include),
		2, 2, "[--delete] include <distribution> <.changes-file>"},
	{"generatefilelists",	A_F(generatefilelists),
		0, 1, "generatefilelists [reread]"},
	{"clearvanished",	A_D(clearvanished)|IS_CLEARVANISHED,
		0, 0, "[--delete] clearvanished"},
	{"processincoming",	A_D(processincoming),
		1, 2, "processincoming <rule-name> [<.changes file>]"},
	{"gensnapshot",		A_R(gensnapshot),
		2, 2, "gensnapshot <distribution> <date or other name>"},
	{"rerunnotifiers",	A_B(rerunnotifiers),
		0, -1, "rerunnotifiers [<distributions>]"},
	{NULL,NULL,0,0,0,NULL}
};
#undef A_N
#undef A_B
#undef A_ROB
#undef A_C
#undef A_F
#undef A_R
#undef A_RF
#undef A_F

static retvalue callaction(const struct action *action, int argc, const char *argv[]) {
	retvalue result, r;
	struct database *database;
	struct strlist dereferencedfilekeys;
	struct distribution *alldistributions = NULL;
	bool deletederef;
	int needs;

	assert(action != NULL);

	if( action->minargs >= 0 && argc < 1 + action->minargs ) {
		fprintf(stderr, "Error: Too few arguments for command '%s'!\nSyntax: reprepro %s\n",
				argv[0], action->wrongargmessage);
		return RET_ERROR;
	}
	if( action->maxargs >= 0 && argc > 1 + action->maxargs ) {
		fprintf(stderr, "Error: Too many arguments for command '%s'!\nSyntax: reprepro %s\n",
				argv[0], action->wrongargmessage);
		return RET_ERROR;
	}
	needs = action->needs;

	if( ISSET(needs, NEED_DATABASE))
		needs |= NEED_CONFIG;
	if( ISSET(needs, NEED_CONFIG) ) {
		r = distribution_readall(confdir, logdir, &alldistributions);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	if( !ISSET(needs, NEED_DATABASE) ) {
		assert( (needs & !NEED_CONFIG) == 0);

		result = action->start(alldistributions, NULL, NULL, argc, argv);
		return result;
	}

	deletederef = ISSET(needs,NEED_DEREF) && !keepunreferenced;

	result = database_create(&database, dbdir, alldistributions,
			fast, ISSET(needs, NEED_NO_PACKAGES),
			ISSET(needs, IS_CLEARVANISHED), ISSET(needs, IS_RO),
			waitforlock);
	if( !RET_IS_OK(result) ) {
		return result;
	}

	if( ISSET(needs,NEED_REFERENCES) )
		result = database_openreferences(database);

	assert( result != RET_NOTHING );
	if( RET_IS_OK(result) ) {

		if( ISSET(needs,NEED_FILESDB) )
			result = database_openfiles(database, mirrordir);

		assert( result != RET_NOTHING );
		if( RET_IS_OK(result) ) {

			if( deletederef ) {
				assert( ISSET(needs,NEED_REFERENCES) );
				assert( ISSET(needs,NEED_REFERENCES) );
				strlist_init(&dereferencedfilekeys);
			}

			if( !interrupted() ) {
				result = action->start(alldistributions,
					database,
					deletederef?&dereferencedfilekeys:NULL,
					argc,argv);

				if( deletederef ) {
					if( dereferencedfilekeys.count > 0 ) {
					    if( RET_IS_OK(result) && !interrupted() ) {
						retvalue r;

						logger_wait();

						if( verbose >= 0 )
					  	    printf(
"Deleting files no longer referenced...\n");
						r = removeunreferencedfiles(
							database,
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
		}
	}
	if( !interrupted() ) {
		logger_wait();
	}
	logger_warn_waiting();
	r = database_close(database);
	RET_UPDATE(result, r);
	r = distribution_freelist(alldistributions);
	RET_ENDUPDATE(result,r);
	database = NULL;
	return result;
}

enum { LO_DELETE=1,
LO_KEEPUNREFERENCED,
LO_KEEPUNNEEDEDLISTS,
LO_NOTHINGISERROR,
LO_NOLISTDOWNLOAD,
LO_ASKPASSPHRASE,
LO_KEEPDIRECTORIES,
LO_FAST,
LO_SKIPOLD,
LO_GUESSGPGTTY,
LO_NODELETE,
LO_NOKEEPUNREFERENCED,
LO_NOKEEPUNNEEDEDLISTS,
LO_NONOTHINGISERROR,
LO_LISTDOWNLOAD,
LO_NOASKPASSPHRASE,
LO_NOKEEPDIRECTORIES,
LO_NOFAST,
LO_NOSKIPOLD,
LO_NOGUESSGPGTTY,
LO_EXPORT,
LO_DISTDIR,
LO_DBDIR,
LO_LOGDIR,
LO_LISTDIR,
LO_OVERRIDEDIR,
LO_CONFDIR,
LO_METHODDIR,
LO_VERSION,
LO_WAITFORLOCK,
LO_SPACECHECK,
LO_SAFETYMARGIN,
LO_DBSAFETYMARGIN,
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

static unsigned long long parse_number(const char *name, const char *argument, long long max) {
	long long l;
	char *p;

	l = strtoll(argument, &p, 10);
	if( p==NULL || *p != '\0' || l < 0 ) {
		fprintf(stderr, "Invalid argument to %s: '%s'\n", name, argument);
		exit(EXIT_FAILURE);
	}
	if( l == LLONG_MAX  || l > max ) {
		fprintf(stderr, "Too large argument for to %s: '%s'\n", name, argument);
		exit(EXIT_FAILURE);
	}
	return l;
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
"     --logdir <dir>:                Directory to put requeted log files in.\n"
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
					r = set_ignore(optarg, false, config_state);
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
					CONFIGSET(keepunreferenced, true);
					break;
				case LO_NOKEEPUNREFERENCED:
					CONFIGSET(keepunreferenced, false);
					break;
				case LO_KEEPUNNEEDEDLISTS:
					CONFIGSET(keepunneededlists, true);
					break;
				case LO_NOKEEPUNNEEDEDLISTS:
					CONFIGSET(keepunneededlists, false);
					break;
				case LO_KEEPDIRECTORIES:
					CONFIGSET(keepdirectories, true);
					break;
				case LO_NOKEEPDIRECTORIES:
					CONFIGSET(keepdirectories, false);
					break;
				case LO_NOTHINGISERROR:
					CONFIGSET(nothingiserror, true);
					break;
				case LO_NONOTHINGISERROR:
					CONFIGSET(nothingiserror, false);
					break;
				case LO_NOLISTDOWNLOAD:
					CONFIGSET(nolistsdownload, true);
					break;
				case LO_LISTDOWNLOAD:
					CONFIGSET(nolistsdownload, false);
					break;
				case LO_ASKPASSPHRASE:
					CONFIGSET(askforpassphrase, true);
					break;
				case LO_NOASKPASSPHRASE:
					CONFIGSET(askforpassphrase, false);
					break;
				case LO_GUESSGPGTTY:
					CONFIGSET(guessgpgtty, true);
					break;
				case LO_NOGUESSGPGTTY:
					CONFIGSET(guessgpgtty, false);
					break;
				case LO_SKIPOLD:
					CONFIGSET(skipold, true);
					break;
				case LO_NOSKIPOLD:
					CONFIGSET(skipold, false);
					break;
				case LO_FAST:
					CONFIGSET(fast, true);
					break;
				case LO_NOFAST:
					CONFIGSET(fast, false);
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
					if( verbose >= -1 )
						fprintf(stderr, "Warning: --overridedir is obsolete. \nPlease put override files in the conf dir for compatibility with future version.\n");
					CONFIGDUP(overridedir,optarg);
					break;
				case LO_CONFDIR:
					CONFIGDUP(confdir,optarg);
					break;
				case LO_LOGDIR:
					CONFIGDUP(logdir,optarg);
					break;
				case LO_METHODDIR:
					CONFIGDUP(methoddir,optarg);
					break;
				case LO_VERSION:
					fprintf(stderr,"%s: This is " PACKAGE " version " VERSION "\n",programname);
					exit(EXIT_SUCCESS);
				case LO_WAITFORLOCK:
					CONFIGSET(waitforlock, parse_number(
							"--waitforlock",
							optarg, SIZE_MAX));
					break;
				case LO_SPACECHECK:
					if( strcasecmp(optarg, "none") == 0 ) {
						CONFIGSET(spacecheckmode, scm_NONE);
					} else if( strcasecmp(optarg, "full") == 0 ) {
						CONFIGSET(spacecheckmode, scm_FULL);
					} else {
						fprintf(stderr,
"Unknown --spacecheck argument: '%s'!\n", optarg);
						exit(EXIT_FAILURE);
					}
					break;
				case LO_SAFETYMARGIN:
					CONFIGSET(reservedotherspace, parse_number(
							"--safetymargin",
							optarg, LONG_MAX));
					break;
				case LO_DBSAFETYMARGIN:
					CONFIGSET(reserveddbspace, parse_number(
							"--dbsafetymargin",
							optarg, LONG_MAX));
					break;
				default:
					fprintf (stderr,"Error parsing arguments!\n");
					exit(EXIT_FAILURE);
			}
			longoption = 0;
			break;
		case 's':
			verbose--;
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
			r = set_ignore(optarg, true, config_state);
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

static volatile bool was_interrupted = false;
static bool interruption_printed = false;

bool interrupted(void) {
	if( was_interrupted ) {
		if( !interruption_printed ) {
			interruption_printed = true;
			fprintf(stderr, "\n\nInterruption in progress, interrupt again to force-stop it (and risking database corruption!)\n\n");
		}
		return true;
	} else
		return false;
}

static void interrupt_signaled(int signal) /*__attribute__((signal))*/;
static void interrupt_signaled(UNUSED(int signal)) {
	was_interrupted = true;
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
		{"logdir", required_argument, &longoption, LO_LOGDIR},
		{"section", required_argument, NULL, 'S'},
		{"priority", required_argument, NULL, 'P'},
		{"component", required_argument, NULL, 'C'},
		{"architecture", required_argument, NULL, 'A'},
		{"type", required_argument, NULL, 'T'},
		{"help", no_argument, NULL, 'h'},
		{"verbose", no_argument, NULL, 'v'},
		{"silent", no_argument, NULL, 's'},
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
		{"guessgpgtty", no_argument, &longoption, LO_GUESSGPGTTY},
		{"noguessgpgtty", no_argument, &longoption, LO_NOGUESSGPGTTY},
		{"nonoguessgpgtty", no_argument, &longoption, LO_GUESSGPGTTY},
		{"fast", no_argument, &longoption, LO_FAST},
		{"nofast", no_argument, &longoption, LO_NOFAST},
		{"skipold", no_argument, &longoption, LO_SKIPOLD},
		{"noskipold", no_argument, &longoption, LO_NOSKIPOLD},
		{"nonoskipold", no_argument, &longoption, LO_SKIPOLD},
		{"force", no_argument, NULL, 'f'},
		{"export", required_argument, &longoption, LO_EXPORT},
		{"waitforlock", required_argument, &longoption, LO_WAITFORLOCK},
		{"checkspace", required_argument, &longoption, LO_SPACECHECK},
		{"spacecheck", required_argument, &longoption, LO_SPACECHECK},
		{"safetymargin", required_argument, &longoption, LO_SAFETYMARGIN},
		{"dbsafetymargin", required_argument, &longoption, LO_DBSAFETYMARGIN},
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

	(void)signal(SIGPIPE, SIG_IGN);

	programname = argv[0];

	init_ignores();

	config_state = CONFIG_OWNER_CMDLINE;

	if( interrupted() )
		exit(EXIT_RET(RET_ERROR_INTERUPTED));

	while( (c = getopt_long(argc,argv,"+fVvshb:P:i:A:C:S:T:",longopts,NULL)) != -1 ) {
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

	if( guessgpgtty && (getenv("GPG_TTY")==NULL) && isatty(0) ) {
		static char terminalname[1024];
		ssize_t len;

		len = readlink("/proc/self/fd/0", terminalname, 1023);
		if( len > 0 && len < 1024 ) {
			terminalname[len] = '\0';
			setenv("GPG_TTY", terminalname, 0);
		} else if( verbose > 10 ) {
			fprintf(stderr, "Could not readlink /proc/self/fd/0 (error was %s), not setting GPG_TTY.\n", strerror(errno));
		}
	}

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
	if( logdir == NULL )
		logdir=calc_dirconcat(mirrordir,"logs");
	if( listdir == NULL )
		listdir=calc_dirconcat(mirrordir,"lists");
	if( overridedir == NULL )
		overridedir=calc_dirconcat(mirrordir,"override");
	if( distdir == NULL || dbdir == NULL || listdir == NULL || logdir == NULL
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
			free(logdir);
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
	free(logdir);
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

