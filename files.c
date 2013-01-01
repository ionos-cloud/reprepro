/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2008 Bernhard R. Link
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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "error.h"
#include "strlist.h"
#include "filecntl.h"
#include "names.h"
#include "checksums.h"
#include "dirs.h"
#include "names.h"
#include "files.h"
#include "ignore.h"
#include "filelist.h"
#include "debfile.h"
#include "pool.h"
#include "database_p.h"

static retvalue files_get_checksums(const char *filekey, /*@out@*/struct checksums **checksums_p) {
	const char *checksums;
	size_t checksumslen;
	retvalue r;

	r = table_gettemprecord(rdb_checksums, filekey,
		&checksums, &checksumslen);
	if (!RET_IS_OK(r))
		return r;
	return checksums_setall(checksums_p, checksums, checksumslen);
}

retvalue files_add_checksums(const char *filekey, const struct checksums *checksums) {
	retvalue r;
	const char *combined;
	size_t combinedlen;

	assert (rdb_checksums != NULL);
	r = checksums_getcombined(checksums, &combined, &combinedlen);
	if (!RET_IS_OK(r))
		return r;
	r = table_adduniqsizedrecord(rdb_checksums, filekey,
			combined, combinedlen + 1, true, false);
	if (!RET_IS_OK(r))
		return r;
	return pool_markadded(filekey);
}

static retvalue files_replace_checksums(const char *filekey, const struct checksums *checksums) {
	retvalue r;
	const char *combined;
	size_t combinedlen;

	assert (rdb_checksums != NULL);
	r = checksums_getcombined(checksums, &combined, &combinedlen);
	if (!RET_IS_OK(r))
		return r;
	return table_adduniqsizedrecord(rdb_checksums, filekey,
			combined, combinedlen + 1, true, false);
}

/* remove file's md5sum from database */
retvalue files_removesilent(const char *filekey) {
	retvalue r;

	if (rdb_contents != NULL)
		(void)table_deleterecord(rdb_contents, filekey, true);
	r = table_deleterecord(rdb_checksums, filekey, true);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Unable to forget unknown filekey '%s'.\n",
				filekey);
		return RET_ERROR_MISSING;
	}
	return r;
}

retvalue files_remove(const char *filekey) {
	retvalue r;

	r = files_removesilent(filekey);
	if (RET_IS_OK(r))
		return pool_markdeleted(filekey);
	return r;
}

/* hardlink file with known checksums and add it to database */
retvalue files_hardlinkandadd(const char *tempfile, const char *filekey, const struct checksums *checksums) {
	retvalue r;

	/* an additional check to make sure nothing tricks us into
	 * overwriting it by another file */
	r = files_canadd(filekey, checksums);
	if (!RET_IS_OK(r))
		return r;
	r = checksums_hardlink(global.outdir, filekey, tempfile, checksums);
	if (RET_WAS_ERROR(r))
		return r;

	return files_add_checksums(filekey, checksums);
}

/* check if file is already there (RET_NOTHING) or could be added (RET_OK)
 * or RET_ERROR_WRONG_MD5SUM if filekey  already has different md5sum */
retvalue files_canadd(const char *filekey, const struct checksums *checksums) {
	retvalue r;
	struct checksums *indatabase;
	bool improves;

	r = files_get_checksums(filekey, &indatabase);
	if (r == RET_NOTHING)
		return RET_OK;
	if (RET_WAS_ERROR(r))
		return r;
	if (!checksums_check(indatabase, checksums, &improves)) {
		fprintf(stderr,
"File \"%s\" is already registered with different checksums!\n",
				filekey);
		checksums_printdifferences(stderr, indatabase, checksums);
		checksums_free(indatabase);
		return RET_ERROR_WRONG_MD5;

	}
	// TODO: sometimes the caller might want to have additional
	// checksums from the database already, think about ways to
	// make them available...
	checksums_free(indatabase);
	return RET_NOTHING;
}


/* check for file in the database and if not found there, if it can be detected */
retvalue files_expect(const char *filekey, const struct checksums *checksums, bool warnifadded) {
	retvalue r;
	char *filename;
	struct checksums *improvedchecksums = NULL;

	r = files_canadd(filekey, checksums);
	if (r == RET_NOTHING)
		return RET_OK;
	if (RET_WAS_ERROR(r))
		return r;

	/* ready to add means missing, so have to look for the file itself: */
	filename = files_calcfullfilename(filekey);
	if (FAILEDTOALLOC(filename))
		return RET_ERROR_OOM;

	/* first check if a possible manually put (or left over from previous
	 * downloads attepts) file is there and is correct */
	r = checksums_test(filename, checksums, &improvedchecksums);
	if (r == RET_ERROR_WRONG_MD5) {
		fprintf(stderr,
"Deleting unexpected file '%s'!\n"
"(not in database and wrong in pool)\n ",
				filename);
		if (unlink(filename) == 0)
			r = RET_NOTHING;
		else {
			int e = errno;
			fprintf(stderr,
"Error %d deleting '%s': %s!\n", e, filename, strerror(e));
		}
	}
	free(filename);
	if (!RET_IS_OK(r))
		return r;

	if (warnifadded)
		fprintf(stderr,
"Warning: readded existing file '%s' mysteriously missing from the checksum database.\n",
				filekey);

	// TODO: some callers might want the updated checksum when
	// improves is true, how to get them there?

	/* add found file to database */
	if (improvedchecksums != NULL) {
		r = files_add_checksums(filekey, improvedchecksums);
		checksums_free(improvedchecksums);
	} else
		r = files_add_checksums(filekey, checksums);
	assert (r != RET_NOTHING);
	return r;
}

/* check for several files in the database and in the pool if missing */
retvalue files_expectfiles(const struct strlist *filekeys, struct checksums *checksumsarray[]) {
	int i;
	retvalue r;

	for (i = 0 ; i < filekeys->count ; i++) {
		const char *filekey = filekeys->values[i];
		const struct checksums *checksums = checksumsarray[i];

		r = files_expect(filekey, checksums, verbose >= 0);
		if (RET_WAS_ERROR(r))
			return r;
		if (r == RET_NOTHING) {
			/* File missing */
			fprintf(stderr, "Missing file %s\n", filekey);
			return RET_ERROR_MISSING;
		}
	}
	return RET_OK;
}

static inline retvalue checkorimprove(const char *filekey, struct checksums **checksums_p) {
	const struct checksums *checksums = *checksums_p;
	struct checksums *indatabase;
	bool improves;
	retvalue r;

	r = files_get_checksums(filekey, &indatabase);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Missing file %s\n", filekey);
		return RET_ERROR_MISSING;
	}
	if (RET_WAS_ERROR(r))
		return r;
	if (!checksums_check(checksums, indatabase, &improves)) {
		fprintf(stderr,
"File \"%s\" is already registered with different checksums!\n",
				filekey);
		checksums_printdifferences(stderr, indatabase, checksums);
		r = RET_ERROR_WRONG_MD5;
	} else if (improves) {
		r = checksums_combine(checksums_p, indatabase, NULL);
	} else
		r = RET_NOTHING;
	checksums_free(indatabase);
	return r;
}


/* check for several files in the database and update information,
 * return RET_NOTHING if everything is OK and nothing needs improving */
retvalue files_checkorimprove(const struct strlist *filekeys, struct checksums *checksumsarray[]) {
	int i;
	retvalue result, r;

	result = RET_NOTHING;
	for (i = 0 ; i < filekeys->count ; i++) {
		r = checkorimprove(filekeys->values[i],
				&checksumsarray[i]);
		if (RET_WAS_ERROR(r))
			return r;
		if (RET_IS_OK(r))
			result = RET_OK;
	}
	return result;
}

/* dump out all information */
retvalue files_printmd5sums(void) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *checksum;

	r = table_newglobalcursor(rdb_checksums, &cursor);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (cursor_nexttemp(rdb_checksums, cursor, &filekey, &checksum)) {
		result = RET_OK;
		(void)fputs(filekey, stdout);
		(void)putchar(' ');
		while (*checksum == ':') {
			while (*checksum != ' ' && *checksum != '\0')
				checksum++;
			if (*checksum == ' ')
				checksum++;
		}
		(void)fputs(checksum, stdout);
		(void)putchar('\n');
	}
	r = cursor_close(rdb_checksums, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue files_printchecksums(void) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *checksum;

	r = table_newglobalcursor(rdb_checksums, &cursor);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (cursor_nexttemp(rdb_checksums, cursor, &filekey, &checksum)) {
		result = RET_OK;
		(void)fputs(filekey, stdout);
		(void)putchar(' ');
		(void)fputs(checksum, stdout);
		(void)putchar('\n');
		if (interrupted()) {
			result = RET_ERROR_INTERRUPTED;
			break;
		}
	}
	r = cursor_close(rdb_checksums, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

/* callback for each registered file */
retvalue files_foreach(per_file_action action, void *privdata) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *checksum;

	r = table_newglobalcursor(rdb_checksums, &cursor);
	if (!RET_IS_OK(r))
		return r;
	result = RET_NOTHING;
	while (cursor_nexttemp(rdb_checksums, cursor, &filekey, &checksum)) {
		if (interrupted()) {
			RET_UPDATE(result, RET_ERROR_INTERRUPTED);
			break;
		}
		r = action(privdata, filekey);
		RET_UPDATE(result, r);
	}
	r = cursor_close(rdb_checksums, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

static retvalue checkpoolfile(const char *fullfilename, const struct checksums *expected, bool *improveable) {
	struct checksums *actual;
	retvalue r;
	bool improves;

	r = checksums_read(fullfilename, &actual);
	if (RET_IS_OK(r)) {
		if (!checksums_check(expected, actual, &improves)) {
			fprintf(stderr, "WRONG CHECKSUMS of '%s':\n",
					fullfilename);
			checksums_printdifferences(stderr, expected, actual);
			r = RET_ERROR_WRONG_MD5;
		} else if (improves)
			*improveable = true;
		checksums_free(actual);
	}
	return r;
}

retvalue files_checkpool(bool fast) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *combined;
	size_t combinedlen;
	struct checksums *expected;
	char *fullfilename;
	bool improveable = false;

	result = RET_NOTHING;
	r = table_newglobalcursor(rdb_checksums, &cursor);
	if (!RET_IS_OK(r))
		return r;
	while (cursor_nexttempdata(rdb_checksums, cursor,
				&filekey, &combined, &combinedlen)) {
		r = checksums_setall(&expected, combined, combinedlen);
		if (RET_WAS_ERROR(r)) {
			RET_UPDATE(result, r);
			continue;
		}
		fullfilename = files_calcfullfilename(filekey);
		if (FAILEDTOALLOC(fullfilename)) {
			result = RET_ERROR_OOM;
			checksums_free(expected);
			break;
		}
		if (fast)
			r = checksums_cheaptest(fullfilename, expected, true);
		else
			r = checkpoolfile(fullfilename, expected, &improveable);
		if (r == RET_NOTHING) {
			fprintf(stderr, "Missing file '%s'!\n", fullfilename);
			r = RET_ERROR_MISSING;
		}
		free(fullfilename);
		checksums_free(expected);
		RET_UPDATE(result, r);
	}
	r = cursor_close(rdb_checksums, cursor);
	RET_ENDUPDATE(result, r);
	if (improveable && verbose >= 0)
		printf(
"There were files with only some of the checksums this version of reprepro\n"
"can compute recorded. To add those run reprepro collectnewchecksums.\n");
	return result;
}

retvalue files_collectnewchecksums(void) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *all;
	size_t alllen;
	struct checksums *expected;
	char *fullfilename;

	result = RET_NOTHING;
	r = table_newglobalcursor(rdb_checksums, &cursor);
	if (!RET_IS_OK(r))
		return r;
	while (cursor_nexttempdata(rdb_checksums, cursor,
				&filekey, &all, &alllen)) {
		r = checksums_setall(&expected, all, alllen);
		if (!RET_IS_OK(r)) {
			RET_UPDATE(result, r);
			continue;
		}
		if (checksums_iscomplete(expected)) {
			checksums_free(expected);
			continue;
		}

		fullfilename = files_calcfullfilename(filekey);
		if (FAILEDTOALLOC(fullfilename)) {
			result = RET_ERROR_OOM;
			checksums_free(expected);
			break;
		}
		r = checksums_complete(&expected, fullfilename);
		if (r == RET_NOTHING) {
			fprintf(stderr, "Missing file '%s'!\n", fullfilename);
			r = RET_ERROR_MISSING;
		}
		if (r == RET_ERROR_WRONG_MD5) {
			fprintf(stderr,
"ERROR: Cannot collect missing checksums for '%s'\n"
"as the file in the pool does not match the already recorded checksums\n",
					filekey);
		}
		free(fullfilename);
		if (RET_IS_OK(r))
			r = files_replace_checksums(filekey, expected);
		checksums_free(expected);
		RET_UPDATE(result, r);
	}
	r = cursor_close(rdb_checksums, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue files_detect(const char *filekey) {
	struct checksums *checksums;
	char *fullfilename;
	retvalue r;

	fullfilename = files_calcfullfilename(filekey);
	if (FAILEDTOALLOC(fullfilename))
		return RET_ERROR_OOM;
	r = checksums_read(fullfilename, &checksums);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Error opening '%s'!\n", fullfilename);
		r = RET_ERROR_MISSING;
	}
	if (RET_WAS_ERROR(r)) {
		free(fullfilename);
		return r;
	}
	free(fullfilename);
	r = files_add_checksums(filekey, checksums);
	checksums_free(checksums);
	return r;
}

struct rfd { bool reread; };

static retvalue regenerate_filelist(void *data, const char *filekey) {
	bool reread = ((struct rfd*)data)->reread;
	size_t l = strlen(filekey);
	char *debfilename;
	char *filelist;
	size_t fls;
	retvalue r;

	if (l <= 4 || memcmp(filekey+l-4, ".deb", 4) != 0)
		return RET_NOTHING;

	if (!reread && !table_recordexists(rdb_contents, filekey))
		return RET_NOTHING;

	debfilename = files_calcfullfilename(filekey);
	if (FAILEDTOALLOC(debfilename))
		return RET_ERROR_OOM;

	r = getfilelist(&filelist, &fls, debfilename);
	free(debfilename);
	if (RET_IS_OK(r)) {
		if (verbose > 0)
			(void)puts(filekey);
		if (verbose > 6) {
			const char *p = filelist;
			while (*p != '\0') {
				(void)putchar(' ');
				(void)puts(p);
				p += strlen(p)+1;
			}
		}
		r = table_adduniqsizedrecord(rdb_contents,
				filekey, filelist, fls, true, true);
		free(filelist);
	}
	return r;
}

retvalue files_regenerate_filelist(bool reread) {
	struct rfd d;

	d.reread = reread;
	return files_foreach(regenerate_filelist, &d);
}

/* Include a yet unknown file into the pool */
retvalue files_preinclude(const char *sourcefilename, const char *filekey, struct checksums **checksums_p) {
	retvalue r;
	struct checksums *checksums, *realchecksums;
	bool improves;
	char *fullfilename;

	r = files_get_checksums(filekey, &checksums);
	if (RET_WAS_ERROR(r))
		return r;
	if (RET_IS_OK(r)) {
		r = checksums_read(sourcefilename, &realchecksums);
		if (r == RET_NOTHING)
			r = RET_ERROR_MISSING;
		if (RET_WAS_ERROR(r)) {
			checksums_free(checksums);
			return r;
		}
		if (!checksums_check(checksums, realchecksums, &improves)) {
			fprintf(stderr,
"ERROR: '%s' cannot be included as '%s'.\n"
"Already existing files can only be included again, if they are the same, but:\n",
				sourcefilename, filekey);
			checksums_printdifferences(stderr, checksums,
					realchecksums);
			checksums_free(checksums);
			checksums_free(realchecksums);
			return RET_ERROR_WRONG_MD5;
		}
		if (improves) {
			r = checksums_combine(&checksums, realchecksums, NULL);
			if (RET_WAS_ERROR(r)) {
				checksums_free(realchecksums);
				checksums_free(checksums);
				return r;
			}
			r = files_replace_checksums(filekey, checksums);
			if (RET_WAS_ERROR(r)) {
				checksums_free(realchecksums);
				checksums_free(checksums);
				return r;
			}
		}
		checksums_free(realchecksums);
		// args, this breaks retvalue semantics!
		if (checksums_p != NULL)
			*checksums_p = checksums;
		else
			checksums_free(checksums);
		return RET_NOTHING;
	}
	assert (sourcefilename != NULL);
	fullfilename = files_calcfullfilename(filekey);
	if (FAILEDTOALLOC(fullfilename))
		return RET_ERROR_OOM;
	(void)dirs_make_parent(fullfilename);
	r = checksums_copyfile(fullfilename, sourcefilename, true, &checksums);
	if (r == RET_ERROR_EXIST) {
		// TODO: deal with already existing files!
		fprintf(stderr, "File '%s' does already exist!\n",
				fullfilename);
	}
	if (r == RET_NOTHING) {
		fprintf(stderr, "Could not open '%s'!\n", sourcefilename);
		r = RET_ERROR_MISSING;
	}
	if (RET_WAS_ERROR(r)) {
		free(fullfilename);
		return r;
	}
	free(fullfilename);

	r = files_add_checksums(filekey, checksums);
	if (RET_WAS_ERROR(r)) {
		checksums_free(checksums);
		return r;
	}
	if (checksums_p != NULL)
		*checksums_p = checksums;
	else
		checksums_free(checksums);
	return RET_OK;
}

static retvalue checkimproveorinclude(const char *sourcedir, const char *basefilename, const char *filekey, struct checksums **checksums_p, bool *improving) {
	retvalue r;
	struct checksums *checksums = NULL;
	bool improves, copied = false;
	char *fullfilename = files_calcfullfilename(filekey);

	if (FAILEDTOALLOC(fullfilename))
		return RET_ERROR_OOM;

	if (checksums_iscomplete(*checksums_p)) {
		r = checksums_cheaptest(fullfilename, *checksums_p, true);
		if (r != RET_NOTHING) {
			free(fullfilename);
			return r;
		}
	} else {
		r = checksums_read(fullfilename, &checksums);
		if (RET_WAS_ERROR(r)) {
			free(fullfilename);
			return r;
		}
	}
	if (r == RET_NOTHING) {
		char *sourcefilename = calc_dirconcat(sourcedir, basefilename);

		if (FAILEDTOALLOC(sourcefilename)) {
			free(fullfilename);
			return RET_ERROR_OOM;
		}

		fprintf(stderr,
"WARNING: file %s was lost!\n"
"(i.e. found in the database, but not in the pool)\n"
"trying to compensate...\n",
				filekey);
		(void)dirs_make_parent(fullfilename);
		r = checksums_copyfile(fullfilename, sourcefilename, false,
				&checksums);
		if (r == RET_ERROR_EXIST) {
			fprintf(stderr,
"File '%s' seems to be missing and existing at the same time!\n"
"To confused to continue...\n",
					fullfilename);
		}
		if (r == RET_NOTHING) {
			fprintf(stderr, "Could not open '%s'!\n",
					sourcefilename);
			r = RET_ERROR_MISSING;
		}
		free(sourcefilename);
		if (RET_WAS_ERROR(r)) {
			free(fullfilename);
			return r;
		}
		copied = true;
	}

	assert (checksums != NULL);

	if (!checksums_check(*checksums_p, checksums, &improves)) {
		if (copied) {
			deletefile(fullfilename);
			fprintf(stderr,
"ERROR: Unexpected content of file '%s/%s'!\n", sourcedir, basefilename);
		} else
// TODO: if the database only listed some of the currently supported checksums,
// and the caller of checkincludefile supplied some (which none yet does), but
// not all (which needs at least three checksums, i.e. not applicaple before
// sha256 get added), then this might also be called if the file in the pool
// just has the same checksums as previously recorded (e.g. a md5sum collision)
// but the new file was listed with another secondary hash than the original.
// In that situation it might be a bit misleading...
			fprintf(stderr,
"ERROR: file %s is damaged!\n"
"(i.e. found in the database, but with different checksums in the pool)\n",
				filekey);
		checksums_printdifferences(stderr, *checksums_p, checksums);
		r = RET_ERROR_WRONG_MD5;
	}
	if (improves) {
		r = checksums_combine(checksums_p, checksums, NULL);
		if (RET_IS_OK(r))
			*improving = true;
	}
	checksums_free(checksums);
	free(fullfilename);
	return r;
}

retvalue files_checkincludefile(const char *sourcedir, const char *basefilename, const char *filekey, struct checksums **checksums_p) {
	char *sourcefilename, *fullfilename;
	struct checksums *checksums;
	retvalue r;
	bool improves;

	assert (*checksums_p != NULL);

	r = files_get_checksums(filekey, &checksums);
	if (RET_WAS_ERROR(r))
		return r;
	if (RET_IS_OK(r)) {
		/* there are three sources now:
		 *  - the checksums from the database (may have some we
		 *    do not even know about, and may miss some we can
		 *    generate)
		 *  - the checksums provided (typically only md5sum,
		 *    as this comes from a .changes or .dsc)
		 *  - the checksums of the file
		 *
		 *  to make things more complicated, the file should only
		 *  be read if needed, as this needs time.
		 *  And it can happen the file got lost in the pool, then
		 *  this is the best place to replace it.
		 */
		if (!checksums_check(checksums, *checksums_p, &improves)) {
			fprintf(stderr,
"ERROR: '%s/%s' cannot be included as '%s'.\n"
"Already existing files can only be included again, if they are the same, but:\n",
				sourcedir, basefilename, filekey);
			checksums_printdifferences(stderr, checksums,
					*checksums_p);
			checksums_free(checksums);
			return RET_ERROR_WRONG_MD5;
		}
		r = RET_NOTHING;
		if (improves)
			r = checksums_combine(&checksums, *checksums_p, NULL);
		if (!RET_WAS_ERROR(r))
			r = checkimproveorinclude(sourcedir,
				basefilename, filekey, &checksums, &improves);
		if (!RET_WAS_ERROR(r) && improves)
			r = files_replace_checksums(filekey, checksums);
		if (RET_IS_OK(r))
			r = RET_NOTHING;
		/* return the combined checksum */
		checksums_free(*checksums_p);
		*checksums_p = checksums;
		return r;
	}

	assert (sourcedir != NULL);
	sourcefilename = calc_dirconcat(sourcedir, basefilename);
	if (FAILEDTOALLOC(sourcefilename))
		return RET_ERROR_OOM;

	fullfilename = files_calcfullfilename(filekey);
	if (FAILEDTOALLOC(fullfilename)) {
		free(sourcefilename);
		return RET_ERROR_OOM;
	}

	(void)dirs_make_parent(fullfilename);
	r = checksums_copyfile(fullfilename, sourcefilename, true, &checksums);
	if (r == RET_NOTHING) {
		fprintf(stderr, "Could not open '%s'!\n", sourcefilename);
		r = RET_ERROR_MISSING;
	}
	if (RET_WAS_ERROR(r)) {
		free(fullfilename);
		free(sourcefilename);
		return r;
	}
	if (!checksums_check(*checksums_p, checksums, &improves)) {
		deletefile(fullfilename);
		fprintf(stderr, "ERROR: Unexpected content of file '%s'!\n",
				sourcefilename);
		checksums_printdifferences(stderr, *checksums_p, checksums);
		r = RET_ERROR_WRONG_MD5;
	}
	free(sourcefilename);
	free(fullfilename);
	if (RET_WAS_ERROR(r)) {
		return r;
	}
	if (improves) {
		r = checksums_combine(checksums_p, checksums, NULL);
		checksums_free(checksums);
		if (RET_WAS_ERROR(r))
			return r;
	} else
		checksums_free(checksums);

	return files_add_checksums(filekey, *checksums_p);
}

off_t files_getsize(const char *filekey) {
	retvalue r;
	off_t s;
	struct checksums *checksums;

	r = files_get_checksums(filekey, &checksums);
	if (!RET_IS_OK(r))
		return -1;
	s = checksums_getfilesize(checksums);
	checksums_free(checksums);
	return s;
}
