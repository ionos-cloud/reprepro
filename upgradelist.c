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
#include "upgradelist.h"

extern int verbose;

typedef struct s_package_data {
	struct s_package_data *next;
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
	//TODO: pointer to data?: char *new_from;

	/* the new control-chunk for the package to go in 
	 * non-NULL if new_version && newversion > version_in_use */
	char *new_control;
	/* the list of files that will belong to this: 
	 * same validity */
	struct strlist new_files;
	struct strlist new_md5sums;
} package_data;

struct s_upgradelist {
	upgrade_decide_function *decide;
	packagesdb packages;
	target target;
	package_data *list;
	/* NULL or the last/next thing to test in alphabetical order */
	package_data *current,*last;
};

static void package_data_free(package_data *data){
	if( data == NULL )
		return;
	free(data->name);
	free(data->version_in_use);
	free(data->new_version);
	//free(data->new_from);
	free(data->new_control);
	strlist_done(&data->new_files);
	strlist_done(&data->new_md5sums);
	free(data);
}


static retvalue save_package_version(void *d,const char *packagename,const char *chunk) {
	upgradelist upgrade = d;
	char *version;
	retvalue r;
	package_data *package;

	r = upgrade->target->getversion(upgrade->target,chunk,&version);
	if( RET_WAS_ERROR(r) )
		return r;

	package = calloc(1,sizeof(package_data));
	if( package == NULL ) {
		free(version);
		return RET_ERROR_OOM;
	}

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

	
retvalue upgradelist_initialize(upgradelist *ul,target t,packagesdb packages,upgrade_decide_function *decide) {
	upgradelist upgrade;
	retvalue r;

	upgrade = calloc(1,sizeof(struct s_upgradelist));
	if( upgrade == NULL )
		return RET_ERROR_OOM;

	upgrade->decide = decide;
	upgrade->packages = packages;
	upgrade->target = t;

	r = packages_foreach(packages,save_package_version,upgrade,0);

	if( RET_WAS_ERROR(r) ) {
		upgradelist_done(upgrade);
		return r;
	}

	upgrade->current = upgrade->list;
	upgrade->last = NULL;

	*ul = upgrade;
	return RET_OK;
}

retvalue upgradelist_done(upgradelist upgrade) {
	package_data *l;
	
	if( ! upgrade )
		return RET_NOTHING;

	l = upgrade->list;
	while( l ) {
		package_data *n = l->next;
		package_data_free(l);
		l = n;
	}
	
	free(upgrade);
	return RET_OK;
}

retvalue upgradelist_trypackage(upgradelist upgrade,const char *chunk){
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
	 * again and again... */

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
		package_data *new;

		decision = (*upgrade->decide)(packagename,NULL,version);
		if( decision != UD_UPGRADE ) {
			free(packagename);
			free(version);
			return RET_NOTHING;
		}

		new = calloc(1,sizeof(package_data));
		if( new == NULL ) {
			free(packagename);
			free(version);
			return RET_ERROR_OOM;
		}
		new->name = packagename;
		packagename = NULL; //to be sure...
		new->new_version = version;
		new->version = version;
		version = NULL; //to be sure...
		r = upgrade->target->getinstalldata(upgrade->target,new->name,new->new_version,chunk,&new->new_control,&new->new_files,&new->new_md5sums);
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
		package_data *current = upgrade->current;
		char *control;struct strlist files,md5sums;


		r = dpkgversions_isNewer(version,current->version);
		if( RET_WAS_ERROR(r) ) {
			free(packagename);
			free(version);
			return r;
		}
		if( r == RET_NOTHING ) {
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
		decision = upgrade->decide(current->name,
					current->version,version);
		if( decision != UD_UPGRADE ) {
			//TODO: perhaps set a flag if hold was applied...
			free(version);
			free(packagename);
			return RET_NOTHING;
		}

		r = upgrade->target->getinstalldata(upgrade->target,packagename,version,chunk,&control,&files,&md5sums);
		free(packagename);
		if( RET_WAS_ERROR(r) ) {
			free(version);
			return r;
		}
		free(current->new_version);
		current->new_version = version;
		current->version = version;
		strlist_move(&current->new_files,&files);
		strlist_move(&current->new_md5sums,&md5sums);
		free(current->new_control);
		current->new_control = control;
	}
	return RET_OK;
}

retvalue upgradelist_update(upgradelist upgrade,const char *filename,int force){

	upgrade->current = upgrade->list;
	upgrade->last = NULL;

	return chunk_foreach(filename,(void*)upgradelist_trypackage,upgrade,force,0);
}

retvalue upgradelist_dump(upgradelist upgrade){
	package_data *pkg;

	pkg = upgrade->list;
	while( pkg ) {
		if( pkg->version == pkg->version_in_use ) {
			if( verbose > 0 )
				printf("'%s': '%s' will be kept " 
				       "(best new: '%s')\n",
				       pkg->name,pkg->version_in_use,
				       pkg->new_version);

		} else {
			printf("'%s': '%s' will be upgraded to '%s':\n " 
			       "files needed: ",
			       pkg->name,pkg->version_in_use,
			       pkg->new_version);
			strlist_fprint(stdout,&pkg->new_files);
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
