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
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>
#include "error.h"
#include "chunks.h"
#include "sources.h"
#include "md5sum.h"
#include "dirs.h"
#include "names.h"
#include "release.h"
#include "updates.h"
#include "copyfile.h"
#include "distribution.h"

retvalue distribution_free(struct distribution *distribution) {
	retvalue result,r;

	if( distribution) {
		free(distribution->codename);
		free(distribution->suite);
		free(distribution->version);
		free(distribution->origin);
		free(distribution->label);
		free(distribution->description);
		free(distribution->signwith);
		free(distribution->override);
		free(distribution->srcoverride);
		strlist_done(&distribution->udebcomponents);
		strlist_done(&distribution->architectures);
		strlist_done(&distribution->components);
		strlist_done(&distribution->updates);

		result = RET_OK;

		while( distribution->targets ) {
			struct target *next = distribution->targets->next;

			r = target_free(distribution->targets);
			RET_UPDATE(result,r);
			distribution->targets = next;
		}
		updates_freetargets(distribution->updatetargets);
		updates_freeorigins(distribution->updateorigins);
		free(distribution);
		return result;
	} else
		return RET_OK;
}

/* create all contained targets... */
static retvalue createtargets(struct distribution *distribution) {
	retvalue r;
	int i,j;
	const char *arch,*comp;
	struct target *t;
	struct target *last = NULL;

	for( i = 0 ; i < distribution->components.count ; i++ ) {
		comp = distribution->components.values[i];
		for( j = 0 ; j < distribution->architectures.count ; j++ ) {
			arch = distribution->architectures.values[j];
			if( strcmp(arch,"source") != 0 ) {
				r = target_initialize_binary(distribution->codename,comp,arch,&t);
				if( RET_IS_OK(r) ) {
					if( last ) {
						last->next = t;
					} else {
						distribution->targets = t;
					}
					last = t;
				}
				if( RET_WAS_ERROR(r) )
					return r;
				if( strlist_in(&distribution->udebcomponents,comp) ) {
					r = target_initialize_ubinary(distribution->codename,comp,arch,&t);
					if( RET_IS_OK(r) ) {
						if( last ) {
							last->next = t;
						} else {
							distribution->targets = t;
						}
						last = t;
					}
					if( RET_WAS_ERROR(r) )
						return r;

				}
			}
			
		}
		/* check if this distribution contains source
		 * (yes, yes, source is not really an architecture, but
		 *  the .changes files started with this...) */
		if( strlist_in(&distribution->architectures,"source") ) {
			r = target_initialize_source(distribution->codename,comp,&t);
			if( last ) {
				last->next = t;
			} else {
				distribution->targets = t;
			}
			last = t;
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return RET_OK;
}

struct distribution_filter {int count; const char **dists; };

static inline bool_t isinfilter(const char *codename, const struct distribution_filter filter){
	int i;

	/* nothing given means all */
	if( filter.count <= 0 )
		return TRUE;

	for( i = 0 ; i < filter.count ; i++ ) {
		if( strcmp((filter.dists)[i],codename) == 0 )
			return TRUE;
	}
	return FALSE;
}

static retvalue distribution_parse_and_filter(struct distribution **distribution,const char *chunk,struct distribution_filter filter) {
	struct distribution *r;
	retvalue ret;
	const char *missing;
static const char * const allowedfields[] = {
"Codename", "Suite", "Version", "Origin", "Label", "Description", 
"Architectures", "Components", "Update", "SignWith", "Override", 
"SourceOverride", "UDebComponents", NULL};

	assert( chunk !=NULL && distribution != NULL );
	
	// TODO: if those are checked anyway, there should be no reason to
	// research them later...
	ret = chunk_checkfields(chunk,allowedfields,TRUE);
	if( RET_WAS_ERROR(ret) )
		return ret;

	r = calloc(1,sizeof(struct distribution));
	if( !r )
		return RET_ERROR_OOM;

#define fieldrequired(name)	if( ret == RET_NOTHING ) { fputs("While parsing distribution definition, required field " name " not found!\n",stderr); ret = RET_ERROR_MISSING; }

	ret = chunk_getvalue(chunk,"Codename",&r->codename);
	fieldrequired("Codename");
	if( RET_IS_OK(ret) )
		ret = propercodename(r->codename);
	if( RET_WAS_ERROR(ret) ) {
		(void)distribution_free(r);
		return ret;
	}

	if( !isinfilter(r->codename,filter) ) {
		(void)distribution_free(r);
		return RET_NOTHING;
	}

#define getpossibleemptyfield(key,fieldname) \
		ret = chunk_getvalue(chunk,key,&r->fieldname); \
		if(RET_WAS_ERROR(ret)) { \
			(void)distribution_free(r); \
			return ret; \
		} else if( ret == RET_NOTHING) \
			r->fieldname = NULL;
#define getpossibleemptywordlist(key,fieldname) \
		ret = chunk_getwordlist(chunk,key,&r->fieldname); \
		if(RET_WAS_ERROR(ret)) { \
			(void)distribution_free(r); \
			return ret; \
		} else if( ret == RET_NOTHING) { \
			r->fieldname.count = 0; \
			r->fieldname.values = NULL; \
		}
		
	getpossibleemptyfield("Suite",suite);
	getpossibleemptyfield("Version",version);
	getpossibleemptyfield("Origin",origin);
	getpossibleemptyfield("Label",label);
	getpossibleemptyfield("Description",description);
	ret = chunk_getwordlist(chunk,"Architectures",&r->architectures);
	fieldrequired("Architectures");
	if( RET_IS_OK(ret) )
		ret = properarchitectures(&r->architectures);
	if( RET_WAS_ERROR(ret) ) {
		(void)distribution_free(r);
		return ret;
	}
	ret = chunk_getwordlist(chunk,"Components",&r->components);
	fieldrequired("Components");
	if( RET_IS_OK(ret) )
		ret = propercomponents(&r->components);
	if( RET_WAS_ERROR(ret) ) {
		(void)distribution_free(r);
		return ret;
	}
	ret = chunk_getwordlist(chunk,"Update",&r->updates);
	if( RET_WAS_ERROR(ret) ) {
		(void)distribution_free(r);
		return ret;
	}
	getpossibleemptyfield("SignWith",signwith);
	getpossibleemptyfield("Override",override);
	getpossibleemptyfield("SourceOverride",srcoverride);

	getpossibleemptywordlist("UDebComponents",udebcomponents);

	// TODO: instead of checking here make sure it can have more
	// in the rest of the code...
	if( !strlist_subset(&r->components,&r->udebcomponents,&missing) ) {
		fprintf(stderr,"In distribution description of '%s':\n"
				"UDebComponent contains '%s' not found in Components!\n",
				r->codename,missing);
		(void)distribution_free(r);
		return ret;
	}

	ret = createtargets(r);
	if( RET_WAS_ERROR(ret) ) {
		(void)distribution_free(r);
		return ret;
	}

	*distribution = r;
	return RET_OK;

#undef fieldrequired
#undef getpossibleemptyfield
#undef getpossibleemptywordlist
}
	
/* call <action> for each part of <distribution>. */
retvalue distribution_foreach_part(const struct distribution *distribution,const char *component,const char *architecture,const char *packagetype,distribution_each_action action,void *data,int force) {
	retvalue result,r;
	struct target *t;

	result = RET_NOTHING;
	for( t = distribution->targets ; t ; t = t->next ) {
		if( component != NULL && strcmp(component,t->component) != 0 )
			continue;
		if( architecture != NULL && strcmp(architecture,t->architecture) != 0 )
			continue;
		if( packagetype != NULL && strcmp(packagetype,t->packagetype) != 0 )
			continue;
		r = action(data,t);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			return result;
	}
	return result;
}

struct target *distribution_getpart(const struct distribution *distribution,const char *component,const char *architecture,const char *packagetype) {
	struct target *t = distribution->targets;

	while( t && ( strcmp(t->component,component) != 0 || strcmp(t->architecture,architecture) || strcmp(t->packagetype,packagetype) )) {
		t = t->next;
	}
	// todo: make sure UDEBs get never called here without real testing...!!!!
	assert(t);
	return t;
}



struct dist_mydata {struct distribution_filter filter; distributionaction *action; void *data;};

static retvalue processdistribution(void *d,const char *chunk) {
	struct dist_mydata *mydata = d;
	retvalue result,r;
	struct distribution *distribution;

	result = distribution_parse_and_filter(&distribution,chunk,mydata->filter);
	if( RET_IS_OK(result) ){

		result = mydata->action(mydata->data,distribution);
		r = distribution_free(distribution);
		RET_ENDUPDATE(result,r);
	}

	return result;
}

retvalue distribution_foreach(const char *conf,int argc,const char *argv[],distributionaction action,void *data,int force) {
	retvalue result;
	char *fn;
	struct dist_mydata mydata;

	mydata.filter.count = argc;
	mydata.filter.dists = argv;
	mydata.data = data;
	mydata.action = action;
	
	fn = calc_dirconcat(conf,"distributions");
	if( !fn ) 
		return RET_ERROR_OOM;

	result = regularfileexists(fn);
	if( RET_WAS_ERROR(result) ) {
		fprintf(stderr,"Could not find '%s'!\n"
"(Have you forgotten to specify a basedir by -b?\n"
"To only set the conf/ dir use --confdir)\n",fn);
		return RET_ERROR_MISSING;
	}
	
	result = chunk_foreach(fn,processdistribution,&mydata,force,FALSE);

	free(fn);
	return result;
}

struct distmatch_mydata {struct distribution_filter filter; struct distribution **distributions;};

static retvalue adddistribution(void *d,const char *chunk) {
	struct distmatch_mydata *mydata = d;
	retvalue result;
	struct distribution *distribution;

	result = distribution_parse_and_filter(&distribution,chunk,mydata->filter);
	if( RET_IS_OK(result) ){
		struct distribution *d;
		for( d=mydata->distributions; d != NULL; d=d->next ) {
			if( strcmp(d->codename,distribution->codename) == 0 ) {
				fprintf(stderr,"Multiple distributions with the common codename: '%s'!\n",d->codename);
				result = RET_ERROR;
			}
		}
		distribution->next = *mydata->distributions;
		*mydata->distributions = distribution;
	}

	return result;
}

/* get all dists from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_getmatched(const char *conf,int argc,const char *argv[],struct distribution **distributions) {
	retvalue result;
	char *fn;
	struct distmatch_mydata mydata;
	struct distribution *d = NULL;

	mydata.filter.count = argc;
	mydata.filter.dists = (const char**)argv;
	mydata.distributions = &d;
	
	fn = calc_dirconcat(conf,"distributions");
	if( !fn ) 
		return RET_ERROR_OOM;

	result = regularfileexists(fn);
	if( RET_WAS_ERROR(result) ) {
		fprintf(stderr,"Could not find '%s'!\n"
"(Have you forgotten to specify a basedir by -b?\n"
"To only set the conf/ dir use --confdir)\n",fn);
		return RET_ERROR_MISSING;
	}
	
	result = chunk_foreach(fn,adddistribution,&mydata,0,FALSE);
	free(fn);

	if( RET_IS_OK(result) ) {
		*distributions = d;
	} else 
		while( d ) {
			struct distribution *next = d->next;
			(void)distribution_free(d);
			d = next;
		}
	
	return result;
}

struct getdist_mydata {struct distribution_filter filter; struct distribution *distribution;};

static retvalue processgetdistribution(void *d,const char *chunk) {
	struct getdist_mydata *mydata = d;
	retvalue result;

	result = distribution_parse_and_filter(&mydata->distribution,chunk,mydata->filter);
	return result;
}

retvalue distribution_get(struct distribution **distribution,const char *conf,const char *name) {
	retvalue result;
	char *fn;
	struct getdist_mydata mydata;

	mydata.filter.count = 1;
	mydata.filter.dists = &name;
	mydata.distribution = NULL;
	
	fn = calc_dirconcat(conf,"distributions");
	if( !fn ) 
		return RET_ERROR_OOM;

	result = regularfileexists(fn);
	if( RET_WAS_ERROR(result) ) {
		fprintf(stderr,"Could not find '%s'!\n"
"(Have you forgotten to specify a basedir by -b?\n"
"To only set the conf/ dir use --confdir)\n",fn);
		return RET_ERROR_MISSING;
	}
	
	result = chunk_foreach(fn,processgetdistribution,&mydata,0,TRUE);

	free(fn);

	if( !RET_WAS_ERROR(result) )
		*distribution = mydata.distribution;
	
	return result;
}

retvalue distribution_export(struct distribution *distribution,
		const char *dbdir, const char *distdir,
		int force, bool_t onlyneeded) {
	struct target *target;
	retvalue result,r;

	result = RET_NOTHING;
	for( target=distribution->targets; target ; target = target->next ) {
		r = target_mkdistdir(target,distdir);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
		r = target_export(target,dbdir,distdir, force, onlyneeded);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			break;
		if( target->hasrelease ) {
			r = release_genrelease(distribution,target,distdir,onlyneeded);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				break;
		}
	}
	if( (!RET_WAS_ERROR(result) || force > 0 ) && 
			!(onlyneeded && result == RET_NOTHING)  ) {
		retvalue r;

		r = release_gen(distribution,distdir,force);
		RET_UPDATE(result,r);
	}
	return result;
}
