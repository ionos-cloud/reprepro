/*  This file is part of "reprepro"
 *  Copyright (C) 2008 Bernhard R. Link
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
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <unistd.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "atoms.h"
#include "pool.h"
#include "reference.h"
#include "files.h"
#include "sources.h"

/* for now save them only in memory. In later times some way to store
 * them on disk would be nice */

static component_t reserved_components = 0;
static void **file_changes_per_component = NULL;
static void *legacy_file_changes = NULL;
bool pool_havedereferenced = false;
bool pool_havedeleted = false;

#define pl_ADDED 1
#define pl_UNREFERENCED 2
#define pl_DELETED 4

static int legacy_compare(const void *a, const void *b) {
	const char *v1 = a, *v2 = b;
	v1++;
	v2++;
	return strcmp(v1, v2);
}

struct source_node {
	void *file_changes;
	char sourcename[];
};

static int source_node_compare(const void *a, const void *b) {
	const struct source_node *v1 = a, *v2 = b;
	return strcmp(v1->sourcename, v2->sourcename);
}

static retvalue split_filekey(const char *filekey, /*@out@*/component_t *component_p, /*@out@*/struct source_node **node_p, /*@out@*/const char **basename_p) {
	const char *p, *source;
	struct source_node *node;
	component_t c;

	if( unlikely(memcmp(filekey, "pool/", 5) != 0) )
		return RET_NOTHING;
	filekey += 5;
	p = strchr(filekey, '/');
	if( unlikely(p == NULL) )
		return RET_NOTHING;
	c = component_find_l(filekey, (size_t)(p - filekey));
	if( unlikely(!atom_defined(c)) )
		return RET_NOTHING;
	p++;
	if( p[0] != '\0' && p[1] == '/' && p[0] != '/' && p[2] == p[0] ) {
		p += 2;
		if( unlikely(p[0] == 'l' && p[1] == 'i' && p[2] == 'b') )
			return RET_NOTHING;
	} else if( p[0] == 'l' && p[1] == 'i' && p[2] == 'b' && p[3] != '\0' &&
			p[4] == '/' && p[5] == 'l' && p[6] == 'i' && p[7] == 'b' &&
			p[3] != '/' && p[8] == p[3] ) {
		p += 5;
	} else
		return RET_NOTHING;
	source = p;
	p = strchr(source, '/');
	if( unlikely(p == NULL) )
		return RET_NOTHING;
	node = malloc(sizeof(struct source_node) + (p - source) + 1);
	if( FAILEDTOALLOC(node) )
		return RET_ERROR_OOM;
	node->file_changes = NULL;
	memcpy(node->sourcename, source, p - source);
	node->sourcename[p - source] = '\0';
	p++;
	*basename_p = p;
	*node_p = node;
	*component_p = c;
	return RET_OK;
}

/* name can be either basename (in a source directory) or a full
 * filekey (in legacy fallback mode) */
static retvalue remember_name(void **root_p, const char *name, char mode, char mode_and) {
	char **p;

	p = tsearch(name - 1, root_p, legacy_compare);
	if( p == NULL )
		return RET_ERROR_OOM;
	if( *p == name - 1 ) {
		size_t l = strlen(name);
		*p = malloc(l + 2);
		if (*p == NULL)
			return RET_ERROR_OOM;
		**p = mode;
		memcpy((*p) + 1, name, l + 1);
	} else {
		**p &= mode_and;
		**p |= mode;
	}
	return RET_OK;
}

static retvalue remember_filekey(const char *filekey, char mode, char mode_and) {
	retvalue r;
	component_t c IFSTUPIDCC(= atom_unknown);
	struct source_node *node IFSTUPIDCC(= NULL), **found;
	const char *basefilename IFSTUPIDCC(= NULL);

	r = split_filekey(filekey, &c, &node, &basefilename);
	if( RET_WAS_ERROR(r) )
		return r;
	if( r == RET_OK ) {
		assert( atom_defined(c) );
		if( c > reserved_components ) {
			void ** h;

			assert( c <= components_count() );

			h = realloc(file_changes_per_component,
					sizeof(struct source_node*) * (c + 1));
			if( FAILEDTOALLOC(h) )
				return RET_ERROR_OOM;
			file_changes_per_component = h;
			while( reserved_components < c ) {
				h[++reserved_components] = NULL;
			}
		}
		assert( file_changes_per_component != NULL );
		found = tsearch(node, &file_changes_per_component[c],
				source_node_compare);
		if( FAILEDTOALLOC(found) )
			return RET_ERROR_OOM;
		if( *found != node ) {
			free(node);
			node = *found;
		}
		return remember_name(&node->file_changes, basefilename,
				mode, mode_and);
	}
	fprintf(stderr, "Warning: strange filekey '%s'!\n", filekey);
	return remember_name(&legacy_file_changes, filekey, mode, mode_and);
}

retvalue pool_dereferenced(const char *filekey) {
	pool_havedereferenced = true;
	return remember_filekey(filekey, pl_UNREFERENCED, 0xFF);
};

retvalue pool_markadded(const char *filekey) {
	return remember_filekey(filekey, pl_ADDED, ~pl_DELETED);
};

/* delete the file and possible parent directories */
static retvalue deletepoolfile(const char *filekey) {
	int err,en;
	char *filename;
	retvalue r;

	if( interrupted() )
		return RET_ERROR_INTERRUPTED;
	filename = files_calcfullfilename(filekey);
	if( filename == NULL )
		return RET_ERROR_OOM;
	err = unlink(filename);
	if( err != 0 ) {
		en = errno;
		r = RET_ERRNO(en);
		if( errno == ENOENT ) {
			fprintf(stderr,"%s not found, forgetting anyway\n",filename);
			free(filename);
			return RET_NOTHING;
		} else {
			fprintf(stderr, "error %d while unlinking %s: %s\n",
					en, filename, strerror(en));
			free(filename);
			return r;
		}
	} else if( !global.keepdirectories ) {
		/* try to delete parent directories, until one gives
		 * errors (hopefully because it still contains files) */
		size_t fixedpartlen = strlen(global.outdir);
		char *p;

		while( (p = strrchr(filename,'/')) != NULL ) {
			/* do not try to remove parts of the mirrordir */
			if( (size_t)(p-filename) <= fixedpartlen+1 )
				break;
			*p ='\0';
			/* try to rmdir the directory, this will
			 * fail if there are still other files or directories
			 * in it: */
			err = rmdir(filename);
			if( err == 0 ) {
				if( verbose > 1 ) {
					printf("removed now empty directory %s\n",filename);
				}
			} else {
				en = errno;
				if( en != ENOTEMPTY ) {
					//TODO: check here if only some
					//other error was first and it
					//is not empty so we do not have
					//to remove it anyway...
					fprintf(stderr,
"ignoring error %d trying to rmdir %s: %s\n", en, filename, strerror(en));
				}
				/* parent directories will contain this one
				 * thus not be empty, in other words:
				 * everything's done */
				break;
			}
		}

	}
	free(filename);
	return RET_OK;
}


retvalue pool_delete(struct database *database, const char *filekey) {
	retvalue r;

	if( verbose >= 1 )
		printf("deleting and forgetting %s\n",filekey);

	r = deletepoolfile(filekey);
	if( RET_WAS_ERROR(r) )
		return r;

	return files_remove(database, filekey);
}

/* called from files_remove: */
retvalue pool_markdeleted(const char *filekey) {
	pool_havedeleted = true;
	return remember_filekey(filekey, pl_DELETED, ~pl_UNREFERENCED);
};

/* libc's twalk misses a callback_data pointer, so we need some temporary
 * global variables: */
static struct database *d;
static retvalue result;
static bool first, onlycount;
static long woulddelete_count;
static component_t current_component;
static const char *sourcename = NULL;

static void removeifunreferenced(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node; const char *filekey;
	retvalue r;

	if( which != leaf && which != postorder)
		return;

	if( interrupted() )
		return;

	node = *(char **)nodep;
	filekey = node + 1;
	if( (*node & pl_UNREFERENCED) == 0 )
		return;
	r = references_isused(d, filekey);
	if( r != RET_NOTHING )
		return;

	if( onlycount ) {
		woulddelete_count++;
		return;
	}

	if( verbose >= 0 && first ) {
		printf("Deleting files no longer referenced...\n");
		first = false;
	}
	if( verbose >= 1 )
		printf("deleting and forgetting %s\n", filekey);
	r = deletepoolfile(filekey);
	RET_UPDATE(result, r);
	if( !RET_WAS_ERROR(r) ) {
		r = files_removesilent(d, filekey);
		RET_UPDATE(result, r);
		if( !RET_WAS_ERROR(r) )
			*node &= ~pl_UNREFERENCED;
		if( RET_IS_OK(r) )
			*node |= pl_DELETED;
	}
}


static void removeifunreferenced2(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node;
	char *filekey;
	retvalue r;

	if( which != leaf && which != postorder)
		return;

	if( interrupted() )
		return;

	node = *(char **)nodep;
	if( (*node & pl_UNREFERENCED) == 0 )
		return;
	filekey = calc_filekey(current_component, sourcename, node + 1);
	r = references_isused(d, filekey);
	if( r != RET_NOTHING ) {
		free(filekey);
		return;
	}
	if( onlycount ) {
		woulddelete_count++;
		free(filekey);
		return;
	}
	if( verbose >= 0 && first ) {
		printf("Deleting files no longer referenced...\n");
		first = false;
	}
	if( verbose >= 1 )
		printf("deleting and forgetting %s\n", filekey);
	r = deletepoolfile(filekey);
	RET_UPDATE(result, r);
	if( !RET_WAS_ERROR(r) ) {
		r = files_removesilent(d, filekey);
		RET_UPDATE(result, r);
		if( !RET_WAS_ERROR(r) )
			*node &= ~pl_UNREFERENCED;
		if( RET_IS_OK(r) )
			*node |= pl_DELETED;
	}
	RET_UPDATE(result, r);
	free(filekey);
}

static void removeunreferenced_from_component(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	struct source_node *node;

	if( which != leaf && which != postorder)
		return;

	if( interrupted() )
		return;

	node = *(struct source_node **)nodep;
	sourcename = node->sourcename;
	twalk(node->file_changes, removeifunreferenced2);
}

retvalue pool_removeunreferenced(struct database *database, bool delete) {
	component_t c;

	if( !delete && verbose <= 0 )
		return RET_NOTHING;

	d = database;
	result = RET_NOTHING;
	first = true;
	onlycount = !delete;
	woulddelete_count = 0;
	for( c = 1 ; c <= reserved_components ; c++ ) {
		assert( file_changes_per_component != NULL );
		current_component = c;
		twalk(file_changes_per_component[c],
				removeunreferenced_from_component);
	}
	twalk(legacy_file_changes, removeifunreferenced);
	d = NULL;
	if( interrupted() )
		result = RET_ERROR_INTERRUPTED;
	if( !delete && woulddelete_count > 0 ) {
		printf(
"%lu files lost their last reference.\n"
"(dumpunreferenced lists such files, use deleteunreferenced to delete them.)\n",
			woulddelete_count);
	}
	return result;
}

static void removeunusednew(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node; const char *filekey;
	retvalue r;

	if( which != leaf && which != postorder)
		return;

	if( interrupted() )
		return;

	node = *(char **)nodep;
	filekey = node + 1;
	if( (*node & (pl_ADDED|pl_DELETED)) != pl_ADDED )
		return;
	r = references_isused(d, filekey);
	if( r != RET_NOTHING )
		return;

	if( onlycount ) {
		woulddelete_count++;
		return;
	}

	if( verbose >= 0 && first ) {
		printf("Deleting files just added to the pool but not used (to avoid use --keepunusednewfiles next time)\n");
		first = false;
	}
	if( verbose >= 1 )
		printf("deleting and forgetting %s\n", filekey);
	r = deletepoolfile(filekey);
	RET_UPDATE(result, r);
	if( !RET_WAS_ERROR(r) ) {
		r = files_removesilent(d, filekey);
		RET_UPDATE(result, r);
		/* don't remove pl_ADDED here, otherwise the hook
		 * script will be told to remove something not added */
		if( !RET_WAS_ERROR(r) )
			*node &= ~pl_UNREFERENCED;
		if( RET_IS_OK(r) )
			*node |= pl_DELETED;
	}
}


static void removeunusednew2(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node;
	char *filekey;
	retvalue r;

	if( which != leaf && which != postorder)
		return;

	if( interrupted() )
		return;

	node = *(char **)nodep;
	if( (*node & (pl_ADDED|pl_DELETED)) != pl_ADDED )
		return;
	filekey = calc_filekey(current_component, sourcename, node + 1);
	r = references_isused(d, filekey);
	if( r != RET_NOTHING ) {
		free(filekey);
		return;
	}
	if( onlycount ) {
		woulddelete_count++;
		free(filekey);
		return;
	}
	if( verbose >= 0 && first ) {
		printf("Deleting files just added to the pool but not used (to avoid use --keepunusednewfiles next time)\n");
		first = false;
	}
	if( verbose >= 1 )
		printf("deleting and forgetting %s\n", filekey);
	r = deletepoolfile(filekey);
	RET_UPDATE(result, r);
	if( !RET_WAS_ERROR(r) ) {
		r = files_removesilent(d, filekey);
		RET_UPDATE(result, r);
		/* don't remove pl_ADDED here, otherwise the hook
		 * script will be told to remove something not added */
		if( !RET_WAS_ERROR(r) )
			*node &= ~pl_UNREFERENCED;
		if( RET_IS_OK(r) )
			*node |= pl_DELETED;
	}
	RET_UPDATE(result, r);
	free(filekey);
}

static void removeunusednew_from_component(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	struct source_node *node;

	if( which != leaf && which != postorder)
		return;

	if( interrupted() )
		return;

	node = *(struct source_node **)nodep;
	sourcename = node->sourcename;
	twalk(node->file_changes, removeunusednew2);
}

void pool_tidyadded(struct database *database, bool delete) {
	component_t c;

	if( !delete && verbose < 0 )
		return;

	d = database;
	result = RET_NOTHING;
	first = true;
	onlycount = !delete;
	woulddelete_count = 0;
	for( c = 1 ; c <= reserved_components ; c++ ) {
		assert( file_changes_per_component != NULL );
		current_component = c;
		twalk(file_changes_per_component[c],
				removeunusednew_from_component);
	}
	// this should not really happen at all, but better safe then sorry:
	twalk(legacy_file_changes, removeunusednew);
	d = NULL;
	if( !delete && woulddelete_count > 0 ) {
		printf(
"%lu files were added but not used.\n"
"The next deleteunreferenced call will delete them.\n",
			woulddelete_count);
	}
	return;

}
