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
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>
#include <db.h>
#include "error.h"
#include "chunks.h"
#include "sources.h"
#include "md5sum.h"
#include "dirs.h"
#include "names.h"
#include "release.h"
#include "updates.h"
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
			}
			
		}
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
	return RET_OK;
}

static retvalue distribution_parse(struct distribution **distribution,const char *chunk) {
	struct distribution *r;
	retvalue ret;

	assert( chunk !=NULL && distribution != NULL );
	
	r = calloc(1,sizeof(struct distribution));
	if( !r )
		return RET_ERROR_OOM;

#define checkret		 if(!RET_IS_OK(ret)) { \
					(void)distribution_free(r); \
					return ret; \
				}
	ret = chunk_getvalue(chunk,"Codename",&r->codename);
	checkret;
	ret = chunk_getvalue(chunk,"Suite",&r->suite);
	checkret;
	ret = chunk_getvalue(chunk,"Version",&r->version);
	checkret;
	ret = chunk_getvalue(chunk,"Origin",&r->origin);
	checkret;
	ret = chunk_getvalue(chunk,"Label",&r->label);
	checkret;
	ret = chunk_getvalue(chunk,"Description",&r->description);
	checkret;
	ret = chunk_getwordlist(chunk,"Architectures",&r->architectures);
	checkret;
	ret = chunk_getwordlist(chunk,"Components",&r->components);
	checkret;
	ret = chunk_getwordlist(chunk,"Update",&r->updates);
	checkret;
	ret = chunk_getvalue(chunk,"SignWith",&r->signwith);
	if( RET_WAS_ERROR(ret) ) {
		(void)distribution_free(r);
		return ret;
	} else if( ret == RET_NOTHING )
		r->signwith = NULL;

	ret = createtargets(r);
	checkret;

	*distribution = r;
	return RET_OK;
}

struct distribution_filter {int count; const char **dists; };

static retvalue distribution_parse_and_filter(struct distribution **distribution,const char *chunk,struct distribution_filter filter) {
	retvalue result;
	int i;
	
	// TODO: combine this with distribution_parse, so that
	// there is no need to read anything in even when it
	// is not the correct one...
	result = distribution_parse(distribution,chunk);
	if( RET_IS_OK(result) ) {
		if( filter.count > 0 ) {
			i = filter.count;
			while( i-- > 0 && strcmp((filter.dists)[i],(*distribution)->codename) != 0 ) {
			}
			if( i < 0 ) {
				(void)distribution_free(*distribution);
				*distribution = NULL;
				return RET_NOTHING;
			}
		}
	}
	return result;
}
	
/* call <action> for each part of <distribution>. */
retvalue distribution_foreach_part(const struct distribution *distribution,const char *component,const char *architecture,distribution_each_action action,void *data,int force) {
	retvalue result,r;
	struct target *t;

	result = RET_NOTHING;
	for( t = distribution->targets ; t ; t = t->next ) {
		if( component != NULL && strcmp(component,t->component) != 0 )
			continue;
		if( architecture != NULL && strcmp(architecture,t->architecture) != 0 )
			continue;
		r = action(data,t);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && force <= 0 )
			return result;
	}
	return result;
}

struct target *distribution_getpart(const struct distribution *distribution,const char *component,const char *architecture) {
	struct target *t = distribution->targets;

	while( t && ( strcmp(t->component,component) != 0 || strcmp(t->architecture,architecture)  )) {
		t = t->next;
	}
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

		result = mydata->action(mydata->data,chunk,distribution);
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
	
	result = chunk_foreach(fn,processdistribution,&mydata,force,0);

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
	
	result = chunk_foreach(fn,adddistribution,&mydata,0,0);
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
	
	result = chunk_foreach(fn,processgetdistribution,&mydata,0,1);

	free(fn);

	if( !RET_WAS_ERROR(result) )
		*distribution = mydata.distribution;
	
	return result;
}

retvalue distribution_export(struct distribution *distribution,
		const char *dbdir, const char *distdir,
		int force, int onlyneeded) {
	struct target *target;
	retvalue result,r;

	result = RET_NOTHING;
	for( target=distribution->targets; target ; target = target->next ) {
		r = release_genrelease(distribution,target,distdir);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force)
			break;
		r = target_export(target,dbdir,distdir, force, onlyneeded);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) && ! force)
			break;
	}
	if( !RET_WAS_ERROR(result) || force ) {
		retvalue r;

		r = release_gen(distribution,distdir,force);
		RET_UPDATE(result,r);
	}
	return result;
}
