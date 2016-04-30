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

#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "donefile.h"
#include "names.h"
#include "checksums.h"
#include "remoterepository.h"

/* This stores what an distribution that is updated from remote repositories
 * has already processed, so that things already processed do not have to be
 * downloaded or processed again. */

struct markdonefile {
	char *finalfilename;
	char *tempfilename;
	FILE *file;
};


static inline char *donefilename(const char *codename) {
	return genlistsfilename("lastseen", 2, "", codename, NULL);
}

retvalue markdone_create(const char *codename, struct markdonefile **done_p) {
	struct markdonefile *done;

	done = NEW(struct markdonefile);
	if (FAILEDTOALLOC(done))
		return RET_ERROR_OOM;
	done->finalfilename = donefilename(codename);
	if (FAILEDTOALLOC(done->finalfilename)) {
		free(done);
		return RET_ERROR_OOM;
	}
	done->tempfilename = calc_addsuffix(done->finalfilename, "new");
	if (FAILEDTOALLOC(done->tempfilename)) {
		free(done->finalfilename);
		free(done);
		return RET_ERROR_OOM;
	}
	done->file = fopen(done->tempfilename, "w+");
	if (done->file == NULL) {
		int e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, done->tempfilename, strerror(e));
		free(done->finalfilename);
		free(done->tempfilename);
		free(done);
		return RET_ERROR;
	}
	fprintf(done->file, "Updates already processed for %s:\n", codename);
	*done_p = done;
	return RET_OK;
}

void markdone_finish(struct markdonefile *done) {
	bool error = false;

	if (done == NULL)
		return;
	if (done->file == NULL)
		error = true;
	else {
		if (ferror(done->file) != 0) {
			fprintf(stderr, "An error occurred writing to '%s'!\n",
					done->tempfilename);
			(void)fclose(done->file);
			error = true;
		} else if (fclose(done->file) != 0) {
			int e = errno;
			fprintf(stderr,
"Error %d occurred writing to '%s': %s!\n",
					e, done->tempfilename, strerror(e));
			error = true;
		}
		done->file = NULL;
	}
	if (error)
		(void)unlink(done->tempfilename);
	else {
		int i;

		i = rename(done->tempfilename, done->finalfilename);
		if (i != 0) {
			int e = errno;
			fprintf(stderr, "Error %d moving '%s' to '%s': %s!\n",
					e, done->tempfilename,
					done->finalfilename, strerror(e));
		}
	}
	free(done->finalfilename);
	free(done->tempfilename);
	free(done);
}

void markdone_target(struct markdonefile *done, const char *identifier) {
	fprintf(done->file, "Target %s\n", identifier);
}

void markdone_index(struct markdonefile *done, const char *file, const struct checksums *checksums) {
	retvalue r;
	size_t s;
	const char *data;

	r = checksums_getcombined(checksums, &data, &s);
	if (!RET_IS_OK(r))
		return;
	fprintf(done->file, "Index %s %s\n", file, data);
}

void markdone_cleaner(struct markdonefile *done) {
	fprintf(done->file, "Delete\n");
}

/* the same for reading */

struct donefile {
	char *filename;
	char *linebuffer;
	size_t linebuffer_size;
	FILE *file;
};

retvalue donefile_open(const char *codename, struct donefile **done_p) {
	struct donefile *done;
	ssize_t s;

	done = zNEW(struct donefile);
	if (FAILEDTOALLOC(done))
		return RET_ERROR_OOM;

	done->filename = donefilename(codename);
	if (FAILEDTOALLOC(done->filename)) {
		free(done);
		return RET_ERROR_OOM;
	}

	done->file = fopen(done->filename, "r");
	if (done->file == NULL) {
		donefile_close(done);
		return RET_NOTHING;
	}
	s = getline(&done->linebuffer, &done->linebuffer_size, done->file);
	if (s <= 0 || done->linebuffer[s-1] != '\n') {
		/* if it cannot be read or is empty or not a text file,
		 * delete it, and do as if it never existed... */
		unlink(done->filename);
		donefile_close(done);
		return RET_NOTHING;
	}
	done->linebuffer[s-1] = '\0';
	// TODO: check the first line?
	*done_p = done;
	return RET_OK;
}

void donefile_close(struct donefile *done) {
	if (done == NULL)
		return;
	// TODO: check return, only print a warning, though,
	// no need to interrupt anything.
	if (done->file != NULL)
		fclose(done->file);
	free(done->linebuffer);
	free(done->filename);
	free(done);
}

retvalue donefile_nexttarget(struct donefile *done, const char **identifier_p) {
	ssize_t s;

	while (strncmp(done->linebuffer, "Target ", 7) != 0) {
		s = getline(&done->linebuffer, &done->linebuffer_size,
				done->file);
		if (s <= 0 || done->linebuffer[s-1] != '\n')
			/* Malformed line, ignore the rest... */
			return RET_NOTHING;
		done->linebuffer[s-1] = '\0';
	}
	/* do not process a second time */
	done->linebuffer[0] = '\0';
	/* and return the identifier part */
	*identifier_p = done->linebuffer + 7;
	return RET_OK;
}

bool donefile_nextindex(struct donefile *done, const char **filename_p, struct checksums **checksums_p) {
	char *p;
	ssize_t s;
	retvalue r;

	s = getline(&done->linebuffer, &done->linebuffer_size, done->file);
	if (s <= 0 || done->linebuffer[s-1] != '\n') {
		done->linebuffer[0] = '\0';
		return false;
	}
	done->linebuffer[s-1] = '\0';
	if (strncmp(done->linebuffer, "Index ", 6) != 0)
		return false;
	p = done->linebuffer + 6;
	*filename_p = p;
	p = strchr(p, ' ');
	if (p == NULL)
		return false;
	*(p++) = '\0';
	r = checksums_parse(checksums_p, p);
	return RET_IS_OK(r);
}

bool donefile_iscleaner(struct donefile *done) {
	ssize_t s;

	s = getline(&done->linebuffer, &done->linebuffer_size, done->file);
	if (s <= 0 || done->linebuffer[s-1] != '\n') {
		done->linebuffer[0] = '\0';
		return false;
	}
	done->linebuffer[s-1] = '\0';
	return strcmp(done->linebuffer, "Delete") == 0;
}
