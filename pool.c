/*  This file is part of "reprepro"
 *  Copyright (C) 2008,2009,2012 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <unistd.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "atoms.h"
#include "strlist.h"
#include "dirs.h"
#include "pool.h"
#include "reference.h"
#include "files.h"
#include "sources.h"
#include "outhook.h"

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
	const char *p, *lastp, *source;
	struct source_node *node;
	component_t c;

	if (unlikely(memcmp(filekey, "pool/", 5) != 0))
		return RET_NOTHING;
	lastp = filekey + 4;
	filekey = lastp + 1;
	/* components can include slashes, so look for the first valid component
	 * followed by something looking like a proper directory.
	 * This might missdetect the component, but as it only is used for
	 * the current run it will hopefully always detect the same place
	 * (and all that is important is that it is the same place) */
	while (true) {
		p = strchr(lastp + 1, '/');
		if (unlikely(p == NULL))
			return RET_NOTHING;
		lastp = p;
		c = component_find_l(filekey, (size_t)(p - filekey));
		if (unlikely(!atom_defined(c)))
			continue;
		p++;
		if (p[0] != '\0' && p[1] == '/' && p[0] != '/' && p[2] == p[0]) {
			p += 2;
			if (unlikely(p[0] == 'l' && p[1] == 'i' && p[2] == 'b'))
				continue;
			source = p;
			break;
		} else if (p[0] == 'l' && p[1] == 'i' && p[2] == 'b' && p[3] != '\0'
				&& p[4] == '/' && p[5] == 'l' && p[6] == 'i'
				&& p[7] == 'b' && p[3] != '/' && p[8] == p[3]) {
			source = p + 5;
			break;
		} else
			continue;
	}
	p = strchr(source, '/');
	if (unlikely(p == NULL))
		return RET_NOTHING;
	node = malloc(sizeof(struct source_node) + (p - source) + 1);
	if (FAILEDTOALLOC(node))
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
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	if (*p == name - 1) {
		size_t l = strlen(name);
		*p = malloc(l + 2);
		if (FAILEDTOALLOC(*p))
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
	component_t c;
	struct source_node *node, **found;
	const char *basefilename;

	r = split_filekey(filekey, &c, &node, &basefilename);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_OK) {
		assert (atom_defined(c));
		if (c > reserved_components) {
			void ** h;

			assert (c <= components_count());

			h = realloc(file_changes_per_component,
					sizeof(void*) * (c + 1));
			if (FAILEDTOALLOC(h))
				return RET_ERROR_OOM;
			file_changes_per_component = h;
			while (reserved_components < c) {
				h[++reserved_components] = NULL;
			}
		}
		assert (file_changes_per_component != NULL);
		found = tsearch(node, &file_changes_per_component[c],
				source_node_compare);
		if (FAILEDTOALLOC(found))
			return RET_ERROR_OOM;
		if (*found != node) {
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

/* so much code, just for the case the morguedir is on another partition than
 * the pool dir... */

static inline retvalue copyfile(const char *source, const char *destination, int outfd, off_t length) {
	int infd, err;
	ssize_t readbytes;
	void *buffer;
	size_t bufsize = 1024*1024;

	buffer = malloc(bufsize);
	if (FAILEDTOALLOC(buffer)) {
		(void)close(outfd);
		(void)unlink(destination);
		bufsize = 16*1024;
		buffer = malloc(bufsize);
		if (FAILEDTOALLOC(buffer))
			return RET_ERROR_OOM;
	}

	infd = open(source, O_RDONLY|O_NOCTTY);
	if (infd < 0) {
		int en = errno;

		fprintf(stderr,
"error %d opening file %s to be copied into the morgue: %s\n",
				en, source, strerror(en));
		free(buffer);
		(void)close(outfd);
		(void)unlink(destination);
		return RET_ERRNO(en);
	}
	while ((readbytes = read(infd, buffer, bufsize)) > 0) {
		const char *start = buffer;

		if ((off_t)readbytes > length) {
			fprintf(stderr,
"Mismatch of sizes of '%s': files is larger than expected!\n",
					destination);
			break;
		}
		while (readbytes > 0) {
			ssize_t written;

			written = write(outfd, start, readbytes);
			if (written > 0) {
				assert (written <= readbytes);
				readbytes -= written;
				start += written;
			} else if (written < 0) {
				int en = errno;

				(void)close(infd);
				(void)close(outfd);
				(void)unlink(destination);
				free(buffer);

				fprintf(stderr,
"error %d writing to morgue file %s: %s\n",
						en, destination, strerror(en));
				return RET_ERRNO(en);
			}
		}
	}
	free(buffer);
	if (readbytes == 0) {
		err = close(infd);
		if (err != 0)
			readbytes = -1;
		infd = -1;
	}
	if (readbytes < 0) {
		int en = errno;
		fprintf(stderr,
"error %d reading file %s to be copied into the morgue: %s\n",
				en, source, strerror(en));
		if (infd >= 0)
			(void)close(infd);
		(void)close(outfd);
		(void)unlink(destination);
		return RET_ERRNO(en);
	}
	if (infd >= 0)
		(void)close(infd);
	err = close(outfd);
	if (err != 0) {
		int en = errno;

		fprintf(stderr, "error %d writing to morgue file %s: %s\n",
				en, destination, strerror(en));
		(void)unlink(destination);
		return RET_ERRNO(en);
	}
	return RET_OK;
}

static inline retvalue morgue_name(const char *filekey, char **name_p, int *fd_p) {
	const char *name = dirs_basename(filekey);
	char *firsttry = calc_dirconcat(global.morguedir, name);
	int fd, en, number;
	retvalue r;

	if (FAILEDTOALLOC(firsttry))
		return RET_ERROR_OOM;

	fd = open(firsttry, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
	if (fd >= 0) {
		assert (fd > 2);
		*name_p = firsttry;
		*fd_p = fd;
		return RET_OK;
	}
	en = errno;
	if (en == ENOENT) {
		r = dirs_make_recursive(global.morguedir);
		if (RET_WAS_ERROR(r)) {
			free(firsttry);
			return r;
		}
		/* try again */
		fd = open(firsttry, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
		if (fd >= 0) {
			assert (fd > 2);
			*name_p = firsttry;
			*fd_p = fd;
			return RET_OK;
		}
		en = errno;
	}
	if (en != EEXIST) {
		fprintf(stderr, "error %d creating morgue-file %s: %s\n",
				en, firsttry, strerror(en));
		free(firsttry);
		return RET_ERRNO(en);
	}
	/* file exists, try names with -number appended: */
	for (number = 1 ; number < 1000 ; number++) {
		char *try = mprintf("%s-%d", firsttry, number);

		if (FAILEDTOALLOC(try)) {
			free(firsttry);
			return RET_ERROR_OOM;
		}
		fd = open(try, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
		if (fd >= 0) {
			assert (fd > 2);
			free(firsttry);
			*name_p = try;
			*fd_p = fd;
			return RET_OK;
		}
		free(try);
	}
	free(firsttry);
	fprintf(stderr, "Could not create a new file '%s' in morguedir '%s'!\n",
			name, global.morguedir);
	return RET_ERROR;
}

/* if file not there, return RET_NOTHING */
static inline retvalue movefiletomorgue(const char *filekey, const char *filename, bool new) {
	char *morguefilename = NULL;
	int err;
	retvalue r;

	if (!new && global.morguedir != NULL) {
		int morguefd = -1;
		struct stat s;

		r = morgue_name(filekey, &morguefilename, &morguefd);
		assert (r != RET_NOTHING);
		if (RET_WAS_ERROR(r))
			return r;
		err = lstat(filename, &s);
		if (err != 0) {
			int en = errno;
			if (errno == ENOENT) {
				(void)close(morguefd);
				(void)unlink(morguefilename);
				free(morguefilename);
				return RET_NOTHING;
			}
			fprintf(stderr,
"error %d looking at file %s to be moved into the morgue: %s\n",
					en, filename, strerror(en));
			(void)close(morguefd);
			(void)unlink(morguefilename);
			free(morguefilename);
			return RET_ERRNO(en);
		}
		if (S_ISLNK(s.st_mode)) {
			/* no need to copy a symbolic link: */
			(void)close(morguefd);
			(void)unlink(morguefilename);
			free(morguefilename);
			morguefilename = NULL;
		} else if (S_ISREG(s.st_mode)) {
			err = rename(filename, morguefilename);
			if (err == 0) {
				(void)close(morguefd);
				free(morguefilename);
				return RET_OK;
			}
			r = copyfile(filename, morguefilename, morguefd,
				       s.st_size);
			if (RET_WAS_ERROR(r)) {
				free(morguefilename);
				return r;
			}
		} else {
			fprintf(stderr,
"Strange (non-regular) file '%s' in the pool.\nPlease delete manually!\n",
					filename);
			(void)close(morguefd);
			(void)unlink(morguefilename);
			free(morguefilename);
			morguefilename = NULL;
			return RET_ERROR;
		}
	}
	err = unlink(filename);
	if (err != 0) {
		int en = errno;
		if (errno == ENOENT)
			return RET_NOTHING;
		fprintf(stderr, "error %d while unlinking %s: %s\n",
			en, filename, strerror(en));
		if (morguefilename != NULL) {
			(void)unlink(morguefilename);
			free(morguefilename);
		}
		return RET_ERRNO(en);
	} else {
		free(morguefilename);
		return RET_OK;
	}
}

/* delete the file and possible parent directories,
 * if not new and morguedir set, first move/copy there */
static retvalue deletepoolfile(const char *filekey, bool new) {
	char *filename;
	retvalue r;

	if (interrupted())
		return RET_ERROR_INTERRUPTED;
	if (!new)
		outhook_send("POOLDELETE", filekey, NULL, NULL);
	filename = files_calcfullfilename(filekey);
	if (FAILEDTOALLOC(filename))
		return RET_ERROR_OOM;
	/* move to morgue or simply delete: */
	r = movefiletomorgue(filekey, filename, new);
	if (r == RET_NOTHING) {
		fprintf(stderr, "%s not found, forgetting anyway\n", filename);
	}
	if (!RET_IS_OK(r)) {
		free(filename);
		return r;
	}
	if (!global.keepdirectories) {
		/* try to delete parent directories, until one gives
		 * errors (hopefully because it still contains files) */
		size_t fixedpartlen = strlen(global.outdir);
		char *p;
		int err, en;

		while ((p = strrchr(filename, '/')) != NULL) {
			/* do not try to remove parts of the mirrordir */
			if ((size_t)(p-filename) <= fixedpartlen+1)
				break;
			*p ='\0';
			/* try to rmdir the directory, this will
			 * fail if there are still other files or directories
			 * in it: */
			err = rmdir(filename);
			if (err == 0) {
				if (verbose > 1) {
					printf(
"removed now empty directory %s\n",
							filename);
				}
			} else {
				en = errno;
				if (en != ENOTEMPTY) {
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


retvalue pool_delete(const char *filekey) {
	retvalue r;

	if (verbose >= 1)
		printf("deleting and forgetting %s\n", filekey);

	r = deletepoolfile(filekey, false);
	if (RET_WAS_ERROR(r))
		return r;

	return files_remove(filekey);
}

/* called from files_remove: */
retvalue pool_markdeleted(const char *filekey) {
	pool_havedeleted = true;
	return remember_filekey(filekey, pl_DELETED, ~pl_UNREFERENCED);
};

/* libc's twalk misses a callback_data pointer, so we need some temporary
 * global variables: */
static retvalue result;
static bool first, onlycount;
static long woulddelete_count;
static component_t current_component;
static const char *sourcename = NULL;

static void removeifunreferenced(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node; const char *filekey;
	retvalue r;

	if (which != leaf && which != postorder)
		return;

	if (interrupted())
		return;

	node = *(char **)nodep;
	filekey = node + 1;
	if ((*node & pl_UNREFERENCED) == 0)
		return;
	r = references_isused(filekey);
	if (r != RET_NOTHING)
		return;

	if (onlycount) {
		woulddelete_count++;
		return;
	}

	if (verbose >= 0 && first) {
		printf("Deleting files no longer referenced...\n");
		first = false;
	}
	if (verbose >= 1)
		printf("deleting and forgetting %s\n", filekey);
	r = deletepoolfile(filekey, (*node & pl_ADDED) != 0);
	RET_UPDATE(result, r);
	if (!RET_WAS_ERROR(r)) {
		r = files_removesilent(filekey);
		RET_UPDATE(result, r);
		if (!RET_WAS_ERROR(r))
			*node &= ~pl_UNREFERENCED;
		if (RET_IS_OK(r))
			*node |= pl_DELETED;
	}
}


static void removeifunreferenced2(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node;
	char *filekey;
	retvalue r;

	if (which != leaf && which != postorder)
		return;

	if (interrupted())
		return;

	node = *(char **)nodep;
	if ((*node & pl_UNREFERENCED) == 0)
		return;
	filekey = calc_filekey(current_component, sourcename, node + 1);
	r = references_isused(filekey);
	if (r != RET_NOTHING) {
		free(filekey);
		return;
	}
	if (onlycount) {
		woulddelete_count++;
		free(filekey);
		return;
	}
	if (verbose >= 0 && first) {
		printf("Deleting files no longer referenced...\n");
		first = false;
	}
	if (verbose >= 1)
		printf("deleting and forgetting %s\n", filekey);
	r = deletepoolfile(filekey, (*node & pl_ADDED) != 0);
	RET_UPDATE(result, r);
	if (!RET_WAS_ERROR(r)) {
		r = files_removesilent(filekey);
		RET_UPDATE(result, r);
		if (!RET_WAS_ERROR(r))
			*node &= ~pl_UNREFERENCED;
		if (RET_IS_OK(r))
			*node |= pl_DELETED;
	}
	RET_UPDATE(result, r);
	free(filekey);
}

static void removeunreferenced_from_component(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	struct source_node *node;

	if (which != leaf && which != postorder)
		return;

	if (interrupted())
		return;

	node = *(struct source_node **)nodep;
	sourcename = node->sourcename;
	twalk(node->file_changes, removeifunreferenced2);
}

retvalue pool_removeunreferenced(bool delete) {
	component_t c;

	if (!delete && verbose <= 0)
		return RET_NOTHING;

	result = RET_NOTHING;
	first = true;
	onlycount = !delete;
	woulddelete_count = 0;
	for (c = 1 ; c <= reserved_components ; c++) {
		assert (file_changes_per_component != NULL);
		current_component = c;
		twalk(file_changes_per_component[c],
				removeunreferenced_from_component);
	}
	twalk(legacy_file_changes, removeifunreferenced);
	if (interrupted())
		result = RET_ERROR_INTERRUPTED;
	if (!delete && woulddelete_count > 0) {
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

	if (which != leaf && which != postorder)
		return;

	if (interrupted())
		return;

	node = *(char **)nodep;
	filekey = node + 1;
	/* only look at newly added and not already deleted */
	if ((*node & (pl_ADDED|pl_DELETED)) != pl_ADDED)
		return;
	r = references_isused(filekey);
	if (r != RET_NOTHING)
		return;

	if (onlycount) {
		woulddelete_count++;
		return;
	}

	if (verbose >= 0 && first) {
		printf(
"Deleting files just added to the pool but not used.\n"
"(to avoid use --keepunusednewfiles next time)\n");
		first = false;
	}
	if (verbose >= 1)
		printf("deleting and forgetting %s\n", filekey);
	r = deletepoolfile(filekey, true);
	RET_UPDATE(result, r);
	if (!RET_WAS_ERROR(r)) {
		r = files_removesilent(filekey);
		RET_UPDATE(result, r);
		/* don't remove pl_ADDED here, otherwise the hook
		 * script will be told to remove something not added */
		if (!RET_WAS_ERROR(r))
			*node &= ~pl_UNREFERENCED;
		if (RET_IS_OK(r))
			*node |= pl_DELETED;
	}
}


static void removeunusednew2(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node;
	char *filekey;
	retvalue r;

	if (which != leaf && which != postorder)
		return;

	if (interrupted())
		return;

	node = *(char **)nodep;
	/* only look at newly added and not already deleted */
	if ((*node & (pl_ADDED|pl_DELETED)) != pl_ADDED)
		return;
	filekey = calc_filekey(current_component, sourcename, node + 1);
	r = references_isused(filekey);
	if (r != RET_NOTHING) {
		free(filekey);
		return;
	}
	if (onlycount) {
		woulddelete_count++;
		free(filekey);
		return;
	}
	if (verbose >= 0 && first) {
		printf(
"Deleting files just added to the pool but not used.\n"
"(to avoid use --keepunusednewfiles next time)\n");
		first = false;
	}
	if (verbose >= 1)
		printf("deleting and forgetting %s\n", filekey);
	r = deletepoolfile(filekey, true);
	RET_UPDATE(result, r);
	if (!RET_WAS_ERROR(r)) {
		r = files_removesilent(filekey);
		RET_UPDATE(result, r);
		/* don't remove pl_ADDED here, otherwise the hook
		 * script will be told to remove something not added */
		if (!RET_WAS_ERROR(r))
			*node &= ~pl_UNREFERENCED;
		if (RET_IS_OK(r))
			*node |= pl_DELETED;
	}
	RET_UPDATE(result, r);
	free(filekey);
}

static void removeunusednew_from_component(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	struct source_node *node;

	if (which != leaf && which != postorder)
		return;

	if (interrupted())
		return;

	node = *(struct source_node **)nodep;
	sourcename = node->sourcename;
	twalk(node->file_changes, removeunusednew2);
}

void pool_tidyadded(bool delete) {
	component_t c;

	if (!delete && verbose < 0)
		return;

	result = RET_NOTHING;
	first = true;
	onlycount = !delete;
	woulddelete_count = 0;
	for (c = 1 ; c <= reserved_components ; c++) {
		assert (file_changes_per_component != NULL);
		current_component = c;
		twalk(file_changes_per_component[c],
				removeunusednew_from_component);
	}
	// this should not really happen at all, but better safe then sorry:
	twalk(legacy_file_changes, removeunusednew);
	if (!delete && woulddelete_count > 0) {
		printf(
"%lu files were added but not used.\n"
"The next deleteunreferenced call will delete them.\n",
			woulddelete_count);
	}
	return;

}

static void reportnewlegacyfiles(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node;

	if (which != leaf && which != postorder)
		return;

	node = *(char **)nodep;
	/* only look at newly added and not already deleted */
	if ((*node & (pl_ADDED|pl_DELETED)) != pl_ADDED)
		return;
	outhook_sendpool(atom_unknown, NULL, node + 1);
}


static void reportnewproperfiles(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	char *node;

	if (which != leaf && which != postorder)
		return;

	node = *(char **)nodep;
	/* only look at newly added and not already deleted */
	if ((*node & (pl_ADDED|pl_DELETED)) != pl_ADDED)
		return;
	outhook_sendpool(current_component, sourcename, node + 1);
}

static void reportnewfiles(const void *nodep, const VISIT which, UNUSED(const int depth)) {
	struct source_node *node;

	if (which != leaf && which != postorder)
		return;

	node = *(struct source_node **)nodep;
	sourcename = node->sourcename;
	twalk(node->file_changes, reportnewproperfiles);
}

void pool_sendnewfiles(void) {
	component_t c;

	for (c = 1 ; c <= reserved_components ; c++) {
		assert (file_changes_per_component != NULL);
		current_component = c;
		twalk(file_changes_per_component[c],
				reportnewfiles);
	}
	twalk(legacy_file_changes, reportnewlegacyfiles);
	return;

}

#ifdef HAVE_TDESTROY
static void sourcename_free(void *n) {
	struct source_node *node = n;

	tdestroy(node->file_changes, free);
	free(node);
}
#endif

void pool_free(void) {
#ifdef HAVE_TDESTROY
	component_t c;

	for (c = 1 ; c <= reserved_components ; c++) {
		tdestroy(file_changes_per_component[c], sourcename_free);
	}
	reserved_components = 0;
	free(file_changes_per_component);
	file_changes_per_component = NULL;
	tdestroy(legacy_file_changes, free);
	legacy_file_changes = NULL;
#endif
}
