/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2004 Bernhard R. Link
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

#include <malloc.h>
#include <string.h>
#include <stdio.h>
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
	/* the version in out represitory: 
	 * NULL means net yet in the archive */
	char *version_in_use;
	/* the most recent version we found 
	 * (either is version_in_use or version_new)*/
	const char *version;

	/* The most recent version we found upstream:
	 * NULL means nothing found. */
	char *new_version;
	/* where the recent version comes from: */
	struct aptmethod *aptmethod;

	/* the new control-chunk for the package to go in 
	 * non-NULL if new_version && newversion > version_in_use */
	char *new_control;
	/* the list of files that will belong to this: 
	 * same validity */
	struct strlist new_filekeys;
	struct strlist new_md5sums;
	struct strlist new_origfiles;
};

struct upgradelist {
	upgrade_decide_function *decide;
	struct target *target;
	struct package_data *list;
	/* NULL or the last/next thing to test in alphabetical order */
	struct package_data *current,*last;
	/* internal...*/
	struct aptmethod *currentaptmethod;
};

static void package_data_free(struct package_data *data){
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

static retvalue save_package_version(void *d,const char *packagename,const char *chunk) {
	struct upgradelist *upgrade = d;
	char *version;
	retvalue r;
	struct package_data *package;

	r = upgrade->target->getversion(upgrade->target,chunk,&version);
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
		upgrade->current = package;
	} else {
		if( strcmp(packagename,upgrade->current->name) > 0 ) {
			upgrade->current->next = package;
			upgrade->current = package;
		} else {
			/* this should only happen if the underlying
			 * database-method get changed, so just throwing
			 * out here */
			fprintf(stderr,"Package-database is not sortet!!!");
			assert(0);
		}
	}

	return RET_OK;
}
	
retvalue upgradelist_initialize(struct upgradelist **ul,struct target *t,const char *dbdir,upgrade_decide_function *decide) {
	struct upgradelist *upgrade;
	retvalue r,r2;

	upgrade = calloc(1,sizeof(struct upgradelist));
	if( upgrade == NULL )
		return RET_ERROR_OOM;

	upgrade->decide = decide;
	upgrade->target = t;

	r = target_initpackagesdb(t,dbdir);
	if( RET_WAS_ERROR(r) ) {
		upgradelist_free(upgrade);
		return r;
	}

	r = packages_foreach(t->packages,save_package_version,upgrade,0);
	r2 = target_closepackagesdb(t);
	RET_UPDATE(r,r2);

	if( RET_WAS_ERROR(r) ) {
		upgradelist_free(upgrade);
		return r;
	}

	upgrade->current = upgrade->list;
	upgrade->last = NULL;

	*ul = upgrade;
	return RET_OK;
}

retvalue upgradelist_free(struct upgradelist *upgrade) {
	struct package_data *l;
	
	if( ! upgrade )
		return RET_NOTHING;

	l = upgrade->list;
	while( l ) {
		struct package_data *n = l->next;
		package_data_free(l);
		l = n;
	}
	
	free(upgrade);
	return RET_OK;
}

static retvalue upgradelist_trypackage(struct upgradelist *upgrade,const char *chunk){
	char *packagename,*version;
	retvalue r;
	upgrade_decision decision;

	r = upgrade->target->getname(upgrade->target,chunk,&packagename);
	if( RET_WAS_ERROR(r) )
		return r;
	r = upgrade->target->getversion(upgrade->target,chunk,&version);
	if( RET_WAS_ERROR(r) ) {
		free(packagename);
		return r;
	}

	/* the algorithm assumes almost all packages are feed in
	 * alphabetically. So the next package will likely be quite
	 * after the last one. Otherwise we walk down the long list
	 * again and again... and again...*/

	if( upgrade->list == NULL ) {
		upgrade->current = NULL;
		upgrade->last = NULL;
	} else while(1) {
		int found;

		assert( upgrade->current != NULL );

		found = strcmp(packagename,upgrade->current->name);

		if( found == 0 )
			break;

		if( found < 0 ) {
			if( upgrade->last == NULL ) {
				/* if we are before the first
				 * package, add us there...*/
				upgrade->current = NULL;
				break;
			}
			// I only hope noone creates indixes anti-sorted:
			if( strcmp(packagename,upgrade->last->name) <= 0 ) {
				/* restart at the beginning: */
				// == 0 has to be restarted, to calc ->last
				upgrade->current = upgrade->list;
				upgrade->last = NULL;
				if( verbose > 10 ) {
					fprintf(stderr,"restarting search...");
				}
				continue;
			}
			/* insert after upgrade->last: */
			upgrade->current = NULL;
			break;
		}
		/* found > 0 : may come later... */
		upgrade->last = upgrade->current;
		upgrade->current = upgrade->current->next;
		if( upgrade->current == NULL ) {
			/* add behind ->last at end of list */
			break;
		}
		/* otherwise repeat until place found */
	}
	if( upgrade->current == NULL ) {
		/* adding a package not yet existing */
		struct package_data *new;

		decision = (*upgrade->decide)(packagename,NULL,version);
		if( decision != UD_UPGRADE ) {
			free(packagename);
			free(version);
			return RET_NOTHING;
		}

		new = calloc(1,sizeof(struct package_data));
		if( new == NULL ) {
			free(packagename);
			free(version);
			return RET_ERROR_OOM;
		}
		assert(upgrade->currentaptmethod);
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
		upgrade->current = new;
		if( upgrade->last ) {
			new->next = upgrade->last->next;
			upgrade->last->next = new;
		} else {
			new->next = upgrade->list;
			upgrade->list = new;
		}
	} else {
		/* The package already exists: */
		struct package_data *current = upgrade->current;
		char *control;struct strlist files,md5sums,origfiles;


		r = dpkgversions_isNewer(version,current->version);
		if( RET_WAS_ERROR(r) ) {
			free(packagename);
			free(version);
			return r;
		}
		if( r == RET_NOTHING ) {
			if( verbose > 30 )
				fprintf(stderr,"Ignoring '%s' from '%s' as not newer than '%s'\n",
				version,packagename,current->version);
			/* there already is a newer version, so
			 * doing nothing but perhaps updating what
			 * versions are around, when we are newer
			 * than yet known candidates... */
			if( current->new_version == NULL || 
			    ( current->new_version != current->version &&
				dpkgversions_isNewer(version,
					current->new_version) == RET_OK ) ) {

				free(current->new_version);
				current->new_version = version;
			} else {
				free(version);
			}

			free(packagename);
			return RET_NOTHING;
		}
		if( verbose > 30 )
			fprintf(stderr,"'%s' from '%s' is newer than '%s' currently\n",
				version,packagename,current->version);
		decision = upgrade->decide(current->name,
					current->version,version);
		if( decision != UD_UPGRADE ) {
			//TODO: perhaps set a flag if hold was applied...
			free(version);
			free(packagename);
			return RET_NOTHING;
		}

		r = upgrade->target->getinstalldata(upgrade->target,packagename,version,chunk,&control,&files,&md5sums,&origfiles);
		free(packagename);
		if( RET_WAS_ERROR(r) ) {
			free(version);
			return r;
		}
		free(current->new_version);
		current->new_version = version;
		current->version = version;
		assert(upgrade->currentaptmethod);
		current->aptmethod = upgrade->currentaptmethod;
		strlist_move(&current->new_filekeys,&files);
		strlist_move(&current->new_md5sums,&md5sums);
		strlist_move(&current->new_origfiles,&origfiles);
		free(current->new_control);
		current->new_control = control;
	}
	return RET_OK;
}

retvalue upgradelist_update(struct upgradelist *upgrade,struct aptmethod *method,const char *filename,int force){

	upgrade->current = upgrade->list;
	upgrade->last = NULL;
	upgrade->currentaptmethod = method;

	return chunk_foreach(filename,(void*)upgradelist_trypackage,upgrade,force,0);
}

retvalue upgradelist_listmissing(struct upgradelist *upgrade,filesdb files){
	struct package_data *pkg;

	pkg = upgrade->list;
	while( pkg ) {
		if( pkg->version == pkg->new_version ) {
			files_printmissing(files,&pkg->new_filekeys,&pkg->new_md5sums,&pkg->new_origfiles);

		}

		pkg = pkg->next;
	}
	return RET_OK;
}

/* request all wanted files in the downloadlists given before */
retvalue upgradelist_enqueue(struct upgradelist *upgrade,struct downloadcache *cache,filesdb filesdb,int force) {
	struct package_data *pkg;
	retvalue result,r;
	pkg = upgrade->list;
	result = RET_NOTHING;
	while( pkg ) {
		if( pkg->version == pkg->new_version ) {
			assert(pkg->aptmethod);
			r = downloadcache_addfiles(cache,filesdb,pkg->aptmethod,
				&pkg->new_origfiles,
				&pkg->new_filekeys,
				&pkg->new_md5sums);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && !force )
				break;
		}
		pkg = pkg->next;
	}
	return result;
}

retvalue upgradelist_install(struct upgradelist *upgrade,const char *dbdir,filesdb files,DB *references,int force){
	struct package_data *pkg;
	retvalue result,r;

	if( upgrade->list == NULL )
		return RET_NOTHING;

	result = target_initpackagesdb(upgrade->target,dbdir);
	if( RET_WAS_ERROR(result) )
		return result;
	pkg = upgrade->list;
	while( pkg ) {
		if( pkg->version == pkg->new_version ) {
			r = files_expectfiles(files,&pkg->new_filekeys,
					&pkg->new_md5sums);
			if( ! RET_WAS_ERROR(r) )
			// TODO: decide what to give to the downgrade flag...
				r = target_addpackage(upgrade->target,
				references,
				pkg->name,pkg->new_version,pkg->new_control,
				&pkg->new_filekeys,force,1);
			RET_UPDATE(result,r);
			if( RET_WAS_ERROR(r) && !force )
				break;
		}
		pkg = pkg->next;
	}
	r = target_closepackagesdb(upgrade->target);
	RET_ENDUPDATE(result,r);
	return result;
}

retvalue upgradelist_dump(struct upgradelist *upgrade){
	struct package_data *pkg;

	pkg = upgrade->list;
	while( pkg ) {
		if( pkg->version == pkg->version_in_use ) {
			if( pkg->new_version ) {
			if( verbose > 1 )
				printf("'%s': '%s' will be kept " 
				       "(best new: '%s')\n",
				       pkg->name,pkg->version_in_use,
				       pkg->new_version);
			} else {
			if( verbose > 0 )
				printf("'%s': '%s' will be kept " 
				       "(unavailable for reload)\n",
				       pkg->name,pkg->version_in_use);
			}

		} else {
			if( pkg->version_in_use ) 
			printf("'%s': '%s' will be upgraded to '%s':\n " 
			       "files needed: ",
			       pkg->name,pkg->version_in_use,
			       pkg->new_version);
			else
			printf("'%s': newly installed as '%s':\n" 
			       "files needed: ",
			       pkg->name, pkg->new_version);
			strlist_fprint(stdout,&pkg->new_filekeys);
			printf("\nwith md5sums: ");
			strlist_fprint(stdout,&pkg->new_md5sums);
			printf("\ninstalling as: '%s'\n",pkg->new_control);
		}

		pkg = pkg->next;
	}
	return RET_OK;
}

/* standard answer function */
upgrade_decision ud_always(const char *package,const char *old_version,const char *new_version) {
	return UD_UPGRADE;
}
