/*  This file is part of "reprepro"
 *  Copyright (C) 2003 Bernhard R. Link
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
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "strlist.h"
#include "dirs.h"
#include "names.h"

/* create directory dirname. */
retvalue dirs_create(const char *dirname) {
	int ret, e;

	ret = mkdir(dirname, 0775);
	if (ret == 0) {
		if (verbose > 1)
			printf("Created directory \"%s\"\n", dirname);
		return RET_OK;
	} else if (ret < 0 && (e = errno) != EEXIST) {
		fprintf(stderr, "Error %d creating directory \"%s\": %s\n",
				e, dirname, strerror(e));
		return RET_ERROR;
	}
	return RET_NOTHING;
}

/* create recursively all parent directories before the last '/' */
retvalue dirs_make_parent(const char *filename) {
	const char *p;
	char *h;
	int i;
	retvalue r;

	for (p = filename+1, i = 1 ; *p != '\0' ; p++, i++) {
		if (*p == '/') {
			h = strndup(filename, i);
			if (FAILEDTOALLOC(h))
				return RET_ERROR_OOM;
			r = dirs_create(h);
			if (RET_WAS_ERROR(r)) {
				free(h);
				return r;
			}
			free(h);
		}
	}
	return RET_OK;
}

/* create dirname and any '/'-separated part of it */
retvalue dirs_make_recursive(const char *directory) {
	retvalue r, result;

	if (interrupted()) {
		return RET_ERROR_INTERRUPTED;
	}
	r = dirs_make_parent(directory);
	result = dirs_create(directory);
	RET_UPDATE(result, r);
	return result;
}

/* create directory and return the number of created directoried */
retvalue dir_create_needed(const char *directory, int *createddepth) {
	retvalue r;
	int ret;
	size_t len = strlen(directory);
	int check, depth = 0;
	char *this;
	int e;

	if (interrupted()) {
		return RET_ERROR_INTERRUPTED;
	}
	while (len > 0 && directory[len-1] == '/')
		len--;
	while (len > 0) {
		this = strndup(directory, len);
		if (FAILEDTOALLOC(this))
			return RET_ERROR_OOM;
		ret = mkdir(this, 0777);
		e = errno;
		if (ret == 0) {
			if (verbose > 1)
				printf("Created directory \"%s\"\n", this);
		} else if (e == EEXIST) {
			free(this);
			break;
		/* normally ENOENT should be the only problem,
		 * but check the others to be nice to annoying filesystems */
		} else if (e != ENOENT && e != EACCES && e != EPERM) {
			fprintf(stderr,
"Cannot create directory \"%s\": %s(%d)\n",
					this, strerror(e), e);
			free(this);
			return RET_ERRNO(e);
		}
		free(this);
		depth++;
		while (len > 0 && directory[len-1] != '/')
			len--;
		while (len > 0 && directory[len-1] == '/')
			len--;
	}
	check = depth;
	while (directory[len] == '/')
		len++;
	while (directory[len] != '\0') {
		while (directory[len] != '\0' && directory[len] != '/')
			len++;
		this = strndup(directory, len);
		if (FAILEDTOALLOC(this))
			return RET_ERROR_OOM;
		r = dirs_create(this);
		free(this);
		if (RET_WAS_ERROR(r))
			return r;
		// TODO: if we get RET_NOTHING here, reduce depth?
		check--;
		while (directory[len] == '/')
			len++;
	}
	assert(check == 0);
	*createddepth = depth;
	return RET_OK;
}

void dir_remove_new(const char *directory, int created) {
	size_t len = strlen(directory);
	char *this;
	int ret;

	while (len > 0 && directory[len-1] == '/')
		len--;
	while (created > 0 && len > 0) {
		this = strndup(directory, len);
		if (FAILEDTOALLOC(this))
			return;
		ret = rmdir(this);
		if (ret == 0) {
			if (verbose > 1)
				printf(
"Removed empty directory \"%s\"\n",
					this);
		} else {
			int e = errno;
			if (e != ENOTEMPTY) {
				fprintf(stderr,
"Error removing directory \"%s\": %s(%d)\n",
						this, strerror(e), e);
			}
			free(this);
			return;
		}
		free(this);
		created--;
		while (len > 0 && directory[len-1] != '/')
			len--;
		while (len > 0 && directory[len-1] == '/')
			len--;
	}
	return;
}

retvalue dirs_getdirectory(const char *filename, char **directory) {
	size_t len;

	assert (filename != NULL && *filename != '\0');

	len = strlen(filename);
	while (len > 1 && filename[len-1] == '/') {
		len--;
	}
	while (len > 0 && filename[len-1] != '/') {
		len--;
	}
	if (len == 0) {
		*directory = strdup(".");
	} else {
		if (len == 1)
			*directory = strdup("/");
		else
			*directory = strndup(filename, len-1);
	}
	if (FAILEDTOALLOC(*directory))
		return RET_ERROR_OOM;
	else
		return RET_OK;

}
const char *dirs_basename(const char *filename) {
	const char *bn;

	bn = strrchr(filename, '/');
	if (bn == NULL)
		return filename;
	// not really suited for the basename of directories,
	// things like /bla/blub/ will give empty string...
	return bn+1;
}

bool isdir(const char *fullfilename) {
	struct stat s;
	int i;

	assert(fullfilename != NULL);
	i = stat(fullfilename, &s);
	return i == 0 && S_ISDIR(s.st_mode);
}
