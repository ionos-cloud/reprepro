/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005 Bernhard R. Link
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

#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "error.h"
#include "strlist.h"
#include "chunks.h"
#include "dpkgversions.h"
#include "target.h"
#include "downloadcache.h"
#include "upgradelist.h"

extern int verbose;

struct package_data {
	struct package_data *next;
	/* the name of the package: */
	char *name;
	/* the version in our repository: 
	 * NULL means not yet in the archive */
	char *version_in_use;
	/* the most recent version we found 
	 * (either is version_in_use or version_new)*/
	/*@dependent@*/const char *version;

	/* if this is != 0, package will be deleted afterwards,
	 * (or new version simply ignored if it is not yet in the
	 * archive) */
	bool_t deleted;

	/* The most recent version we found upstream:
	 * NULL means nothing found. */
	char *new_version;
	/* where the recent version comes from: */
	/*@dependent@*/struct aptmethod *aptmethod;

	/* the new control-chunk for the package to go in 
	 * non-NULL if new_version && newversion == version_in_use */
	char *new_control;
	/* the list of files that will belong to this: 
	 * same validity */
	struct strlist new_filekeys;
	struct strlist new_md5sums;
	struct strlist new_origfiles;
};

struct upgradelist {
	/*@dependent@*/struct target *target;
	struct package_data *list;
	/* package the next package will most probably be after. 
	 * (NULL=before start of list) */
	/*@null@*//*@dependent@*/struct package_data *last;
	/* internal...*/
	/*@dependent@*/struct aptmethod *currentaptmethod;
	/*@temp@*/upgrade_decide_function *predecide;
	/*@temp@*/void *predecide_data;
};

static void package_data_free(/*@only@*/struct package_data *data){
	if( data == NULL )
		return;
	free(data->name);
	free(data->version_in_use);
	free(data->new_version);
	//free(data->new_from);
	free(data->new_control);
	strlist_done(&data->new_filekeys);
	strlist_done(&data->new_md5sums);
	strlist_done(&data->new_origfiles);
	free(data);
}

/* This is called before any package lists are read for any package we already
 * have in this target. upgrade->list points to the first in the sorted list,
 * upgrade->last to the last one inserted */
static retvalue save_package_version(void *d,const char *packagename,const char *chunk) {
	struct upgradelist *upgrade = d;
	char *version;
	retvalue r;
	struct package_data *package;

	r = (*upgrade->target->getversion)(upgrade->target,chunk,&version);
	if( RET_WAS_ERROR(r) )
		return r;

	package = calloc(1,sizeof(struct package_data));
	if( package == NULL ) {
		free(version);
		return RET_ERROR_OOM;
	}

	package->aptmethod = NULL;
	package->name = strdup(packagename);
	if( package->name == NULL ) {
		free(package);
		free(version);
		return RET_ERROR_OOM;
	}
	package->version_in_use = version;
	version = NULL; // just to be sure...
	package->version = package->version_in_use;
	
	if( upgrade->list == NULL ) {
		/* first chunk to add: */
		upgrade->list = package;
		upgrade->last = package;
	} else {
		if( strcmp(packagename,upgrade->last->name) > 0 ) {
			upgrade->last->next = package;
			upgrade->last = package;
		} else {
			/* this should only happen if the underlying
			 * database-method get changed, so just throwing
			 * out here */
			fprintf(stderr,"Package-database is not sorted!!!");
			assert(FALSE);
			exit(EXIT_FAILURE);
		}
	}

	return RET_OK;
}
	
retvalue upgradelist_initialize(struct upgradelist **ul,struct target *t,const char *dbdir) {
	struct upgradelist *upgrade;
	retvalue r,r2;

	upgrade = calloc(1,sizeof(struct upgradelist));
	if( upgrade == NULL )
		return RET_ERROR_OOM;

	upgrade->target = t;

	r = target_initpackagesdb(t,dbdir);
	if( RET_WAS_ERROR(r) ) {
		(void)upgradelist_free(upgrade);
		return r;
	}

	/* Beginn with the packages currently in the archive */
	r = packages_foreach(t->packages,save_package_version,upgrade,0);
	r2 = target_closepackagesdb(t);
	RET_UPDATE(r,r2);

	if( RET_WAS_ERROR(r) ) {
		(void)upgradelist_free(upgrade);
		return r;
	}

	upgrade->last = NULL;

	*ul = upgrade;
	return RET_OK;
}

retvalue upgradelist_free(struct upgradelist *upgrade) {
	struct package_data *l;
	
	if( upgrade == NULL )
		return RET_NOTHING;

	l = upgrade->list;
	while( l != NULL ) {
		struct package_data *n = l->next;
		package_data_free(l);
		l = n;
	}
	
	free(upgrade);
	return RET_OK;
}

static retvalue upgradelist_trypackage(void *data,const char *chunk){
	struct upgradelist *upgrade = data;
	char *packagename,*version;
	retvalue r;
	upgrade_decision decision;
	struct package_data *current,*insertafter;

	r = (*upgrade->target->getname)(upgrade->target,chunk,&packagename);
	if( RET_WAS_ERROR(r) )
		return r;
	r = (*upgrade->target->getversion)(upgrade->target,chunk,&version);
	if( RET_WAS_ERROR(r) ) {
		free(packagename);
		return r;
	}

	/* insertafter = NULL will mean insert before list */
	insertafter = upgrade->last;
	/* the next one to test, current = NULL will mean not found */
	if( insertafter != NULL ) 
		current = insertafter->next;
	else
		current = upgrade->list;

	/* the algorithm assumes almost all packages are feed in
	 * alphabetically. So the next package will likely be quite
	 * after the last one. Otherwise we walk down the long list
	 * again and again... and again... and even some more...*/

	while( TRUE ) {
		int cmp;

		assert( insertafter == NULL || insertafter->next == current );
		assert( insertafter != NULL || current == upgrade->list );

		if( current == NULL )
			cmp = -1; /* every package is before the end of list */
		else
			cmp = strcmp(packagename,current->name);

		if( cmp == 0 )
			break;

		if( cmp < 0 ) {
			int precmp;

			if( insertafter == NULL ) {
				/* if we are before the first
				 * package, add us there...*/
				current = NULL;
				break;
			}
			// I only hope noone creates indices anti-sorted:
			precmp = strcmp(packagename,insertafter->name);
			if( precmp == 0 ) {
				current = insertafter;
				break;
			} else if( precmp < 0 ) {
				/* restart at the beginning: */
				current = upgrade->list;
				insertafter = NULL;
				if( verbose > 10 ) {
					fprintf(stderr,"restarting search...");
				}
				continue;
			} else { // precmp > 0 
				/* insert after insertafter: */
				current = NULL;
				break;
			}
			assert( "This is not reached" == NULL );
		}
		/* cmp > 0 : may come later... */
		assert( current != NULL );
		insertafter = current;
		current = current->next;
		if( current == NULL ) {
			/* add behind insertafter at end of list */
			break;
		}
		/* otherwise repeat until place found */
	}
	if( current == NULL ) {
		/* adding a package not yet known */
		struct package_data *new;

		decision = upgrade->predecide(upgrade->predecide_data,packagename,NULL,version,chunk);
		if( decision != UD_UPGRADE ) {
			upgrade->last = insertafter;
			free(packagename);
			free(version);
			return (decision==UD_ERROR)?RET_ERROR:RET_NOTHING;
		}

		new = calloc(1,sizeof(struct package_data));
		if( new == NULL ) {
			free(packagename);
			free(version);
			return RET_ERROR_OOM;
		}
		assert(upgrade->currentaptmethod!=NULL);
		new->deleted = FALSE; //to be sure...
		new->aptmethod = upgrade->currentaptmethod;
		new->name = packagename;
		packagename = NULL; //to be sure...
		new->new_version = version;
		new->version = version;
		version = NULL; //to be sure...
		r = upgrade->target->getinstalldata(upgrade->target,new->name,new->new_version,chunk,&new->new_control,&new->new_filekeys,&new->new_md5sums,&new->new_origfiles);
		if( RET_WAS_ERROR(r) ) {
			package_data_free(new);
			return RET_ERROR_OOM;
		}
		if( insertafter != NULL ) {
			new->next = insertafter->next;
			insertafter->next = new;
		} else {
			new->next = upgrade->list;
			upgrade->list = new;
		}
		upgrade->last = new;
	} else {
		/* The package already exists: */
		char *control;struct strlist files,md5sums,origfiles;
		int versioncmp;

		upgrade->last = current;

		r = dpkgversions_cmp(version,current->version,&versioncmp);
		if( RET_WAS_ERROR(r) ) {
			free(packagename);
			free(version);
			return r;
		}
		if( versioncmp <= 0 && !current->deleted ) {
			/* there already is a newer version, so
			 * doing nothing but perhaps updating what
			 * versions are around, when we are newer
			 * than yet known candidates... */
			int c = 0;

			if( current->new_version == current->version )
				c =versioncmp;
			else if( current->new_version == NULL )
				c = 1;
			else (void)dpkgversions_cmp(version,
					       current->new_version,&c);

			if( c > 0 ) {
				free(current->new_version);
				current->new_version = version;
			} else
				free(version);

			free(packagename);
			return RET_NOTHING;
		}
		if( versioncmp > 0 && verbose > 30 )
			fprintf(stderr,"'%s' from '%s' is newer than '%s' currently\n",
				version,packagename,current->version);
		decision = upgrade->predecide(upgrade->predecide_data,current->name,
				current->version,version,chunk);
		if( decision != UD_UPGRADE ) {
			/* Even if we do not install it, setting it on hold
			 * will keep it or even install from a mirror before
			 * the delete was applied */
			if( decision == UD_HOLD )
				current->deleted = FALSE;
			free(version);
			free(packagename);
			return (decision==UD_ERROR)?RET_ERROR:RET_NOTHING;
		}

		if( versioncmp == 0 ) {
		/* we are replacing a package with the same version,
		 * so we keep the old one for sake of speed. */
		// TODO: add switch to force reimport of everything...
			current->deleted = FALSE;
			free(version);
			free(packagename);
			return RET_NOTHING;
		}
		if( versioncmp != 0 && current->version == current->new_version 
				&& current->version_in_use != NULL ) {
			/* The version to include is not the newest after the
			 * last deletion round), but maybe older, maybe newer.
			 * So we get to the question: it is also not the same 
			 * like the version we already have? */
			int vcmp = 1;
			(void)dpkgversions_cmp(version,current->version_in_use,&vcmp);
			if( vcmp == 0 ) {
				current->deleted = FALSE;
				current->version = current->version_in_use;
				free(version);
				free(packagename);
				return RET_NOTHING;
			}
		}

// TODO: the following case might be worth considering, but sadly new_version
// might have changed without the proper data set.
//		if( versioncmp >= 0 && current->version == current->version_in_use 
//				&& current->new_version != NULL ) {

		r = upgrade->target->getinstalldata(upgrade->target,packagename,version,chunk,&control,&files,&md5sums,&origfiles);
		free(packagename);
		if( RET_WAS_ERROR(r) ) {
			free(version);
			return r;
		}
		current->deleted = FALSE;
		free(current->new_version);
		current->new_version = version;
		current->version = version;
		assert(upgrade->currentaptmethod!=NULL);
		current->aptmethod = upgrade->currentaptmethod;
		strlist_move(&current->new_filekeys,&files);
		strlist_move(&current->new_md5sums,&md5sums);
		strlist_move(&current->new_origfiles,&origfiles);
		free(current->new_control);
		current->new_control = control;
	}
	return RET_OK;
}

retvalue upgradelist_update(struct upgradelist *upgrade,struct aptmethod *method,const char *filename,upgrade_decide_function *decide,void *decide_data,int force){

	upgrade->last = NULL;
	upgrade->currentaptmethod = method;
	upgrade->predecide = decide;
	upgrade->predecide_data = decide_data;

	return chunk_foreach(filename,upgradelist_trypackage,upgrade,force,FALSE);
}

/* mark all packages as deleted, so they will vanis unless readded or reholded */
retvalue upgradelist_deleteall(struct upgradelist *upgrade) {
	struct package_data *pkg;

	for( pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next ) {
		pkg->deleted = TRUE;
	}

	return RET_OK;
}

retvalue upgradelist_listmissing(struct upgradelist *upgrade,filesdb files){
	struct package_data *pkg;

	for( pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next ) {
		if( pkg->version == pkg->new_version ) {
			retvalue r;
			r = files_printmissing(files,
					&pkg->new_filekeys,&pkg->new_md5sums,
					&pkg->new_origfiles);
			if( RET_WAS_ERROR(r) )
				return r;

		}

	}
	return RET_OK;
}

/* request all wanted files in the downloadlists given before */
retvalue upgradelist_enqueue(struct upgradelist *upgrade,struct downloadcache *cache,filesdb filesdb,int force) {
	struct package_data *pkg;
	retvalue result,r;
	result = RET_NOTHING;
	assert(upgrade != NULL);
	for( pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next ) {
		if( pkg->version == pkg->new_version && !pkg->deleted) {
			assert(pkg->aptmethod != NULL);
			r = downloadcache_addfiles(cache,filesdb,pkg->aptmethod,
				&pkg->new_origfiles,
				&pkg->new_filekeys,
				&pkg->new_md5sums);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				break;
		}
	}
	return result;
}

retvalue upgradelist_install(struct upgradelist *upgrade,const char *dbdir,filesdb files,references refs,int force, bool_t ignoredelete, struct strlist *dereferencedfilekeys){
	struct package_data *pkg;
	retvalue result,r;

	if( upgrade->list == NULL )
		return RET_NOTHING;

	result = target_initpackagesdb(upgrade->target,dbdir);
	if( RET_WAS_ERROR(result) )
		return result;
	result = RET_NOTHING;
	for( pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next ) {
		if( pkg->version == pkg->new_version && !pkg->deleted ) {
			r = files_expectfiles(files,&pkg->new_filekeys,
					&pkg->new_md5sums);
			if( ! RET_WAS_ERROR(r) )
				/* upgrade (or possibly downgrade) */
// TODO: trackingdata?
				r = target_addpackage(upgrade->target,refs,
				pkg->name,pkg->new_version,pkg->new_control,
				&pkg->new_filekeys,TRUE,dereferencedfilekeys,NULL,0);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				break;
		}
		if( pkg->deleted && pkg->version_in_use != NULL && !ignoredelete ) {
			r = target_removepackage(upgrade->target,refs,pkg->name,dereferencedfilekeys,NULL);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && force <= 0 )
				break;
		}
	}
	r = target_closepackagesdb(upgrade->target);
	RET_ENDUPDATE(result,r);
	return result;
}

void upgradelist_dump(struct upgradelist *upgrade){
	struct package_data *pkg;

	for( pkg = upgrade->list ; pkg != NULL ; pkg = pkg->next ) {
		if( pkg->deleted ) {
			if( pkg->version_in_use != NULL && 
					pkg->new_version != NULL ) {
				printf("'%s': '%s' will be deleted"
				       " (best new: '%s')\n",
				       pkg->name,pkg->version_in_use,
				       pkg->new_version);
			} else if( pkg->version_in_use != NULL ) {
				printf("'%s': '%s' will be deleted"
					" (no longer available)\n",
					pkg->name,pkg->version_in_use);
			} else {
				printf("'%s': will NOT be added as '%s'\n",
						pkg->name,pkg->new_version);
			}
		} else {
			if( pkg->version == pkg->version_in_use ) {
				if( pkg->new_version != NULL ) {
					if( verbose > 1 )
					printf("'%s': '%s' will be kept"
					       " (best new: '%s')\n",
					       pkg->name,pkg->version_in_use,
					       pkg->new_version);
				} else {
					if( verbose > 0 )
					printf("'%s': '%s' will be kept"
					" (unavailable for reload)\n",
					pkg->name,pkg->version_in_use);
				}

			} else {
				if( pkg->version_in_use != NULL ) 
					(void)printf("'%s': '%s' will be upgraded"
					       " to '%s':\n files needed: ",
						pkg->name,pkg->version_in_use,
						pkg->new_version);
				else
					(void)printf("'%s': newly installed"
					       " as '%s':\n files needed: ",
					       pkg->name, pkg->new_version);
				(void)strlist_fprint(stdout,&pkg->new_filekeys);
				(void)printf("\nwith md5sums: ");
				(void)strlist_fprint(stdout,&pkg->new_md5sums);
				(void)printf("\ninstalling as: '%s'\n",pkg->new_control);
			}
		}
	}
}

/* standard answer function */
upgrade_decision ud_always(UNUSED(void *privdata),UNUSED(const char *package),UNUSED(const char *old_version),UNUSED(const char *new_version),UNUSED(const char *new_controlchunk)) {
	return UD_UPGRADE;
}
