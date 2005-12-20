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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA
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
#include "copyfile.h"
#include "tracking.h"
#include "distribution.h"

retvalue distribution_free(struct distribution *distribution) {
	retvalue result,r;

	if( distribution != NULL ) {
		free(distribution->codename);
		free(distribution->suite);
		free(distribution->version);
		free(distribution->origin);
		free(distribution->label);
		free(distribution->description);
		free(distribution->signwith);
		free(distribution->deb_override);
		free(distribution->udeb_override);
		free(distribution->dsc_override);
		strlist_done(&distribution->udebcomponents);
		strlist_done(&distribution->architectures);
		strlist_done(&distribution->components);
		strlist_done(&distribution->updates);
		exportmode_done(&distribution->dsc);
		exportmode_done(&distribution->deb);
		exportmode_done(&distribution->udeb);
		result = RET_OK;

		while( distribution->targets != NULL ) {
			struct target *next = distribution->targets->next;

			r = target_free(distribution->targets);
			RET_UPDATE(result,r);
			distribution->targets = next;
		}
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
				r = target_initialize_binary(distribution->codename,comp,arch,&distribution->deb,&t);
				if( RET_IS_OK(r) ) {
					if( last != NULL ) {
						last->next = t;
					} else {
						distribution->targets = t;
					}
					last = t;
				}
				if( RET_WAS_ERROR(r) )
					return r;
				if( strlist_in(&distribution->udebcomponents,comp) ) {
					r = target_initialize_ubinary(distribution->codename,comp,arch,&distribution->udeb,&t);
					if( RET_IS_OK(r) ) {
						if( last != NULL ) {
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
			r = target_initialize_source(distribution->codename,comp,&distribution->dsc,&t);
			if( last != NULL ) {
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

struct distribution_filter {int count; const char **dists; bool_t *found;};

static inline retvalue isinfilter(const char *codename, const struct distribution_filter filter){
	int i;

	/* nothing given means all */
	if( filter.count <= 0 )
		return TRUE;

	for( i = 0 ; i < filter.count ; i++ ) {
		if( strcmp((filter.dists)[i],codename) == 0 ) {
			if( filter.found[i] ) {
				fprintf(stderr,"Multiple distribution definitions with the common codename: '%s'!\n",codename);
				return RET_ERROR;

			}
			filter.found[i] = TRUE;
			return RET_OK;
		}
	}
	return RET_NOTHING;
}

static retvalue distribution_parse_and_filter(struct distribution **distribution,const char *chunk,struct distribution_filter filter) {
	struct distribution *r;
	retvalue ret;
	const char *missing;
	char *option;
static const char * const allowedfields[] = {
"Codename", "Suite", "Version", "Origin", "Label", "Description", 
"Architectures", "Components", "Update", "SignWith", "DebOverride",
"UDebOverride", "DscOverride", "Tracking",
"UDebComponents", "DebIndices", "DscIndices", "UDebIndices",
NULL};

	assert( chunk !=NULL && distribution != NULL );
	
	// TODO: if those are checked anyway, there should be no reason to
	// research them later...
	ret = chunk_checkfields(chunk,allowedfields,TRUE);
	if( RET_WAS_ERROR(ret) )
		return ret;

	r = calloc(1,sizeof(struct distribution));
	if( r == NULL )
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

	ret = isinfilter(r->codename,filter);
	if( !RET_IS_OK(ret) ) {
		(void)distribution_free(r);
		return ret;
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
	getpossibleemptyfield("DebOverride",deb_override);
	getpossibleemptyfield("UDebOverride",udeb_override);
	getpossibleemptyfield("DscOverride",dsc_override);

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

	ret = chunk_getvalue(chunk,"UDebIndices",&option);
	if(RET_WAS_ERROR(ret)) {
		(void)distribution_free(r);
		return ret;
	} else if( ret == RET_NOTHING)
		option = NULL;
	ret = exportmode_init(&r->udeb,TRUE,NULL,"Packages",option);
	if(RET_WAS_ERROR(ret)) {
		(void)distribution_free(r);
		return ret;
	}

	ret = chunk_getvalue(chunk,"DebIndices",&option);
	if(RET_WAS_ERROR(ret)) {
		(void)distribution_free(r);
		return ret;
	} else if( ret == RET_NOTHING)
		option = NULL;
	ret = exportmode_init(&r->deb,TRUE,"Release","Packages",option);
	if(RET_WAS_ERROR(ret)) {
		(void)distribution_free(r);
		return ret;
	}

	ret = chunk_getvalue(chunk,"DscIndices",&option);
	if(RET_WAS_ERROR(ret)) {
		(void)distribution_free(r);
		return ret;
	} else if( ret == RET_NOTHING)
		option = NULL;
	ret = exportmode_init(&r->dsc,FALSE,"Release","Sources",option);
	if(RET_WAS_ERROR(ret)) {
		(void)distribution_free(r);
		return ret;
	}

	ret = chunk_getvalue(chunk,"Tracking",&option);
	if(RET_WAS_ERROR(ret)) {
		(void)distribution_free(r);
		return ret;
	} else if( ret == RET_NOTHING)
		option = NULL;
	ret = tracking_parse(option,r);
	if(RET_WAS_ERROR(ret)) {
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
	for( t = distribution->targets ; t != NULL ; t = t->next ) {
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

	while( t != NULL && ( strcmp(t->component,component) != 0 || strcmp(t->architecture,architecture) != 0 || strcmp(t->packagetype,packagetype) != 0 )) {
		t = t->next;
	}
	// todo: make sure UDEBs get never called here without real testing...!!!!
	assert( t != NULL );
	return t;
}

struct distmatch_mydata {struct distribution_filter filter; struct distribution *distributions;};

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
		distribution->next = mydata->distributions;
		mydata->distributions = distribution;
	}

	return result;
}

/* get all dists from <conf> fitting in the filter given in <argc,argv> */
retvalue distribution_getmatched(const char *conf,int argc,const char *argv[],struct distribution **distributions) {
	retvalue result;
	char *fn;
	struct distmatch_mydata mydata;

	mydata.filter.count = argc;
	mydata.filter.dists = (const char**)argv;
	mydata.filter.found = calloc(argc,sizeof(bool_t));
	if( mydata.filter.found == NULL )
		return RET_ERROR_OOM;
	mydata.distributions = NULL;
	
	fn = calc_dirconcat(conf,"distributions");
	if( fn == NULL ) 
		return RET_ERROR_OOM;

	result = regularfileexists(fn);
	if( RET_WAS_ERROR(result) ) {
		fprintf(stderr,"Could not find '%s'!\n"
"(Have you forgotten to specify a basedir by -b?\n"
"To only set the conf/ dir use --confdir)\n",fn);
		free(mydata.filter.found);
		free(fn);
		return RET_ERROR_MISSING;
	}
	
	result = chunk_foreach(fn,adddistribution,&mydata,0,FALSE);

	if( !RET_WAS_ERROR(result) ) {
		int i;
		for( i = 0 ; i < argc ; i++ ) {
			if( !mydata.filter.found[i] ) {
				fprintf(stderr,"No distribution definition of '%s' found in '%s'!\n",mydata.filter.dists[i],fn);
				result = RET_ERROR_MISSING;
			}
		}
	}
	free(fn);
	if( result == RET_NOTHING ) {
		/* if argc==0 and no definition in conf/distributions */
		fprintf(stderr,"No distribution definitons found!\n");
		result = RET_ERROR_MISSING;
	}

	if( RET_IS_OK(result) ) {
		*distributions = mydata.distributions;
	} else  {
		while( mydata.distributions != NULL ) {
			struct distribution *next = mydata.distributions->next;
			(void)distribution_free(mydata.distributions);
			mydata.distributions = next;
		}
	}
	free(mydata.filter.found);
	return result;
}

retvalue distribution_get(struct distribution **distribution,const char *confdir,const char *name) {
	retvalue result;
	struct distribution *d;

	/* This is a bit overkill, as it does not stop when it finds the
	 * definition of the distribution. But this way we can warn
	 * about emtpy lines in the definition (as this would split
	 * it in two definitions, the second one no valid one). 
	 */
	result = distribution_getmatched(confdir,1,&name,&d);

	if( RET_WAS_ERROR(result) )
		return result;

	if( result == RET_NOTHING ) {
		fprintf(stderr,"Cannot find definition of distribution '%s' in %s/distributions!\n",name,confdir);
		return RET_ERROR_MISSING;
	}
	assert( d != NULL && d->next == NULL );

	*distribution = d;
	return RET_OK;
}

retvalue distribution_export(struct distribution *distribution,
		const char *confdir, const char *dbdir, const char *distdir,
		bool_t onlyneeded) {
	struct target *target;
	retvalue result,r;
	struct release *release;

	assert( distribution != NULL );

	r = release_init(dbdir,distdir,distribution->codename,&release);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;
	for( target=distribution->targets; target != NULL ; target = target->next ) {
		r = target_mkdistdir(target,distdir);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		r = target_export(target,confdir,dbdir,onlyneeded,release);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
		if( target->exportmode->release != NULL ) {
			r = release_directorydescription(release,distribution,target,target->exportmode->release,onlyneeded);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) )
				break;
		}
	}
	if( RET_WAS_ERROR(result) )
		release_free(release);
	else {
		retvalue r;

		r = release_write(release,distribution,onlyneeded);
		RET_UPDATE(result,r);
	}
	return result;
}

retvalue distribution_freelist(struct distribution *distributions) {
	retvalue result,r;

	result = RET_NOTHING;
	while( distributions != NULL ) {
		struct distribution *d = distributions->next;
		r = distribution_free(distributions);
		RET_UPDATE(result,r);
		distributions = d;
	}
	return result;
}

retvalue distribution_exportandfreelist(struct distribution *distributions,
		const char *confdir,const char *dbdir, const char *distdir) {
	retvalue result,r;

	result = RET_NOTHING;
	while( distributions != NULL ) {
		struct distribution *d = distributions->next;

		r = distribution_export(distributions,confdir,dbdir,distdir,TRUE);
		RET_UPDATE(result,r);
		
		r = distribution_free(distributions);
		RET_ENDUPDATE(result,r);
		distributions = d;
	}
	return result;
}
