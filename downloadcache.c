/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007,2009 Bernhard R. Link
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
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "files.h"
#include "freespace.h"
#include "downloadcache.h"


struct downloaditem {
	/*@dependent@*//*@null@*/struct downloaditem *parent;
	/*@null@*/struct downloaditem *left, *right;
	char *filekey;
	struct checksums *checksums;
	bool done;
};

/* Initialize a new download session */
retvalue downloadcache_initialize(enum spacecheckmode mode, off_t reserveddb, off_t reservedother, struct downloadcache **download) {
	struct downloadcache *cache;
	retvalue r;

	cache = zNEW(struct downloadcache);
	if (FAILEDTOALLOC(cache))
		return RET_ERROR_OOM;
	r = space_prepare(&cache->devices, mode, reserveddb, reservedother);
	if (RET_WAS_ERROR(r)) {
		free(cache);
		return r;
	}
	*download = cache;
	return RET_OK;
}

/* free all memory */
static void freeitem(/*@null@*//*@only@*/struct downloaditem *item) {
	if (item == NULL)
		return;
	freeitem(item->left);
	freeitem(item->right);
	free(item->filekey);
	checksums_free(item->checksums);
	free(item);
}

retvalue downloadcache_free(struct downloadcache *download) {
	if (download == NULL)
		return RET_NOTHING;

	freeitem(download->items);
	space_free(download->devices);
	free(download);
	return RET_OK;
}

static retvalue downloaditem_callback(enum queue_action action, void *privdata, void *privdata2, const char *uri, const char *gotfilename, const char *wantedfilename, /*@null@*/const struct checksums *checksums, const char *method) {
	struct downloaditem *d = privdata;
	struct downloadcache *cache = privdata2;
	struct checksums *read_checksums = NULL;
	retvalue r;
	bool improves;

	if (action != qa_got)
		// TODO: instead store in downloaditem?
		return RET_ERROR;

	/* if the file is somewhere else, copy it: */
	if (strcmp(gotfilename, wantedfilename) != 0) {
		if (verbose > 1)
			fprintf(stderr,
"Linking file '%s' to '%s'...\n", gotfilename, wantedfilename);
		r = checksums_linkorcopyfile(wantedfilename, gotfilename,
				&read_checksums);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Cannot open '%s', obtained from '%s' method.\n",
					gotfilename, method);
			r = RET_ERROR_MISSING;
		}
		if (RET_WAS_ERROR(r)) {
			// TODO: instead store in downloaditem?
			return r;
		}
		if (read_checksums != NULL)
			checksums = read_checksums;
	}

	if (checksums == NULL || !checksums_iscomplete(checksums)) {
		assert(read_checksums == NULL);
		r = checksums_read(wantedfilename, &read_checksums);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Cannot open '%s', though '%s' method claims to have put it there!\n",
					wantedfilename, method);
			r = RET_ERROR_MISSING;
		}
		if (RET_WAS_ERROR(r)) {
			// TODO: instead store in downloaditem?
			return r;
		}
		checksums = read_checksums;
	}
	assert (checksums != NULL);

	if (!checksums_check(d->checksums, checksums, &improves)) {
		fprintf(stderr, "Wrong checksum during receive of '%s':\n",
				uri);
		checksums_printdifferences(stderr, d->checksums, checksums);
		checksums_free(read_checksums);
		(void)unlink(wantedfilename);
		// TODO: instead store in downloaditem?
		return RET_ERROR_WRONG_MD5;
	}
	if (improves) {
		r = checksums_combine(&d->checksums, checksums, NULL);
		checksums_free(read_checksums);
		if (RET_WAS_ERROR(r))
			return r;
	} else
		checksums_free(read_checksums);

	if (global.showdownloadpercent > 0) {
		unsigned int percent;

		cache->size_done += checksums_getfilesize(d->checksums);

		percent = (100 * cache->size_done) / cache->size_todo;
		if (global.showdownloadpercent > 1
				|| percent > cache->last_percent) {
			unsigned long long all = cache->size_done;
			int kb, mb, gb, tb, b, groups = 0;

			cache->last_percent = percent;

			printf("Got %u%%: ", percent);
			b = all & 1023;
			all = all >> 10;
			kb = all & 1023;
			all = all >> 10;
			mb = all & 1023;
			all = all >> 10;
			gb = all & 1023;
			all = all >> 10;
			tb = all;
			if (tb != 0) {
				printf("%dT ", tb);
				groups++;
			}
			if (groups < 2 && (groups > 0 || gb != 0)) {
				printf("%dG ", gb);
				groups++;
			}
			if (groups < 2 && (groups > 0 || mb != 0)) {
				printf("%dM ", mb);
				groups++;
			}
			if (groups < 2 && (groups > 0 || kb != 0)) {
				printf("%dK ", kb);
				groups++;
			}
			if (groups < 2 && (groups > 0 || b != 0))
				printf("%d ", b);
			puts("bytes");
		}
	}
	r = files_add_checksums(d->filekey, d->checksums);
	if (RET_WAS_ERROR(r))
		return r;
	d->done = true;
	return RET_OK;
}

/*@null@*//*@dependent@*/ static struct downloaditem *searchforitem(struct downloadcache *list,
					const char *filekey,
					/*@out@*/struct downloaditem **p,
					/*@out@*/struct downloaditem ***h) {
	struct downloaditem *item;
	int c;

	*h = &list->items;
	*p = NULL;
	item = list->items;
	while (item != NULL) {
		*p = item;
		c = strcmp(filekey, item->filekey);
		if (c == 0)
			return item;
		else if (c < 0) {
			*h = &item->left;
			item = item->left;
		} else {
			*h = &item->right;
			item = item->right;
		}
	}
	return NULL;
}

/* queue a new file to be downloaded:
 * results in RET_ERROR_WRONG_MD5, if someone else already asked
 * for the same destination with other md5sum created. */
retvalue downloadcache_add(struct downloadcache *cache, struct aptmethod *method, const char *orig, const char *filekey, const struct checksums *checksums) {

	struct downloaditem *i;
	struct downloaditem *item, **h, *parent;
	char *fullfilename;
	retvalue r;

	assert (cache != NULL && method != NULL);
	r = files_expect(filekey, checksums, false);
	if (r != RET_NOTHING)
		return r;

	i = searchforitem(cache, filekey, &parent, &h);
	if (i != NULL) {
		bool improves;

		assert (i->filekey != NULL);
		if (!checksums_check(i->checksums, checksums, &improves)) {
			fprintf(stderr,
"ERROR: Same file is requested with conflicting checksums:\n");
			checksums_printdifferences(stderr,
					i->checksums, checksums);
			return RET_ERROR_WRONG_MD5;
		}
		if (improves) {
			r = checksums_combine(&i->checksums,
					checksums, NULL);
			if (RET_WAS_ERROR(r))
				return r;
		}
		return RET_NOTHING;
	}
	item = zNEW(struct downloaditem);
	if (FAILEDTOALLOC(item))
		return RET_ERROR_OOM;

	item->done = false;
	item->filekey = strdup(filekey);
	item->checksums = checksums_dup(checksums);
	if (FAILEDTOALLOC(item->filekey) || FAILEDTOALLOC(item->checksums)) {
		freeitem(item);
		return RET_ERROR_OOM;
	}

	fullfilename = files_calcfullfilename(filekey);
	if (FAILEDTOALLOC(fullfilename)) {
		freeitem(item);
		return RET_ERROR_OOM;
	}
	(void)dirs_make_parent(fullfilename);
	r = space_needed(cache->devices, fullfilename, checksums);
	if (RET_WAS_ERROR(r)) {
		free(fullfilename);
		freeitem(item);
		return r;
	}
	r = aptmethod_enqueue(method, orig, fullfilename,
			downloaditem_callback, item, cache);
	if (RET_WAS_ERROR(r)) {
		freeitem(item);
		return r;
	}
	item->left = item->right = NULL;

	item->parent = parent;
	*h = item;

	cache->size_todo += checksums_getfilesize(item->checksums);

	return RET_OK;
}

/* some as above, only for more files... */
retvalue downloadcache_addfiles(struct downloadcache *cache, struct aptmethod *method, const struct checksumsarray *origfiles, const struct strlist *filekeys) {
	retvalue result, r;
	int i;

	assert (origfiles != NULL && filekeys != NULL
		&& origfiles->names.count == filekeys->count);

	result = RET_NOTHING;

	for (i = 0 ; i < filekeys->count ; i++) {
		r = downloadcache_add(cache, method,
			origfiles->names.values[i],
			filekeys->values[i],
			origfiles->checksums[i]);
		RET_UPDATE(result, r);
	}
	return result;
}
