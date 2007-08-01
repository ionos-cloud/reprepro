/*  This file is part of "reprepro"
 *  Copyright (C) 2007 Bernhard R. Link
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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "globals.h"
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "database.h"
#include "dirs.h"
#include "files.h"
#include "reference.h"
#include "database_p.h"

extern int verbose;


static void database_free(struct database *db) {
	if( db == NULL )
		return;
	free(db->directory);
	free(db);
}

retvalue database_create(struct database **result, const char *dbdir) {
	struct database *n;

	n = calloc(1, sizeof(struct database));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->directory = strdup(dbdir);
	if( n->directory == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}

	*result = n;
	return RET_OK;
}

/**********************/
/* lock file handling */
/**********************/

retvalue database_lock(struct database *db, size_t waitforlock) {
	char *lockfile;
	int fd;
	retvalue r;
	size_t tries = 0;

	// TODO: create directory
	r = dirs_make_recursive(db->directory);
	if( RET_WAS_ERROR(r) )
		return r;

	lockfile = calc_dirconcat(db->directory, "lockfile");
	if( lockfile == NULL )
		return RET_ERROR_OOM;
	fd = open(lockfile,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NOCTTY,S_IRUSR|S_IWUSR);
	while( fd < 0 ) {
		int e = errno;
		if( e == EEXIST ) {
			if( tries < waitforlock && ! interrupted() ) {
				if( verbose >= 0 )
					printf("Could not aquire lock: %s already exists!\nWaiting 10 seconds before trying again.\n", lockfile);
				sleep(10);
				tries++;
				fd = open(lockfile,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NOCTTY,S_IRUSR|S_IWUSR);
				continue;

			}
			fprintf(stderr,
"The lockfile '%s' already exists, there might be another instance with the\n"
"same database dir running. To avoid locking overhead, only one process\n"
"can access the database at the same time. Only delete the lockfile if\n"
"you are sure no other version is still running!\n", lockfile);

		} else
			fprintf(stderr,"Error creating lockfile '%s': %d=%m!\n",lockfile,e);
		free(lockfile);
		return RET_ERRNO(e);
	}
	// TODO: do some more locking of this file to avoid problems
	// with the non-atomity of O_EXCL with nfs-filesystems...
	if( close(fd) != 0 ) {
		int e = errno;
		fprintf(stderr,"(Late) Error creating lockfile '%s': %d=%m!\n",lockfile,e);
		(void)unlink(lockfile);
		free(lockfile);
		return RET_ERRNO(e);
	}
	free(lockfile);
	db->locked = TRUE;
	return RET_OK;
}

static void releaselock(struct database *db) {
	char *lockfile;

	lockfile = calc_dirconcat(db->directory, "lockfile");
	if( lockfile == NULL )
		return;
	if( unlink(lockfile) != 0 ) {
		int e = errno;
		fprintf(stderr,"Error deleting lockfile '%s': %d=%m!\n",lockfile,e);
		(void)unlink(lockfile);
	}
	free(lockfile);
	db->locked = FALSE;
}

retvalue database_close(struct database *db) {
	retvalue result = RET_OK, r;
	if( db->references != NULL) {
		r = references_done(db->references);
		db->references = NULL;
		RET_UPDATE(result, r);
	}
	if( db->files ) {
		r = files_done(db->files);
		db->files = NULL;
		RET_UPDATE(result, r);
	}
	if( db->locked )
		releaselock(db);
	database_free(db);
	return RET_OK;
}

retvalue database_openfiles(struct database *db, const char *mirrordir) {
	retvalue r;

	assert( db->files == NULL );

	r = files_initialize(&db->files, db->directory, mirrordir);
	if( !RET_IS_OK(r) )
		db->files = NULL;
	return r;
}

retvalue database_openreferences(struct database *db) {
	retvalue r;

	assert( db->references == NULL );

	r = references_initialize(&db->references, db->directory);
	if( !RET_IS_OK(r) )
		db->references = NULL;
	return r;
}
