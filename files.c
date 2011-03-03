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
#include <malloc.h>
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
#include "database_p.h"

extern int verbose;

static retvalue files_get_checksums(struct database *database, const char *filekey, /*@out@*/struct checksums **checksums_p) {
	const char *md5sum = NULL;
	const char *checksums;
	size_t checksumslen;
	retvalue r;

	if( database->oldmd5sums != NULL ) {
		/* if there is an old-style files.db, then this is the
		 * official source: */
		r = table_gettemprecord(database->oldmd5sums, filekey, &md5sum, NULL);
		if( !RET_IS_OK(r) )
			return r;
		r = table_gettemprecord(database->checksums, filekey,
				&checksums, &checksumslen);
		if( RET_WAS_ERROR(r) )
			return r;
		if( r == RET_NOTHING )
			return checksums_parse(checksums_p, md5sum);
	} else {
		r = table_gettemprecord(database->checksums, filekey,
			&checksums, &checksumslen);
		if( !RET_IS_OK(r) )
			return r;
	}
	return checksums_setall(checksums_p, checksums, checksumslen, md5sum);
}

retvalue files_add_checksums(struct database *database, const char *filekey, const struct checksums *checksums) {
	retvalue r;
	const char *combined;
	size_t combinedlen;

	if( database->oldmd5sums != NULL ) {
		const char *md5sum;

		md5sum = checksums_getmd5sum(checksums);
		r = table_adduniqrecord(database->oldmd5sums, filekey, md5sum);
	}
	assert( database->checksums != NULL );
	r = checksums_getcombined(checksums, &combined, &combinedlen);
	if( !RET_IS_OK(r) )
		return r;
	return table_adduniqsizedrecord(database->checksums, filekey,
			combined, combinedlen + 1, true, false);
}

static retvalue files_replace_checksums(struct database *database, const char *filekey, const struct checksums *checksums) {
	retvalue r;
	const char *combined;
	size_t combinedlen;

	if( database->oldmd5sums != NULL ) {
		const char *md5sum;

		md5sum = checksums_getmd5sum(checksums);
		r = table_adduniqsizedrecord(database->oldmd5sums, filekey,
				md5sum, strlen(md5sum) + 1, true, false);
	}
	assert( database->checksums != NULL );
	r = checksums_getcombined(checksums, &combined, &combinedlen);
	if( !RET_IS_OK(r) )
		return r;
	return table_adduniqsizedrecord(database->checksums, filekey,
			combined, combinedlen + 1, true, false);
}

/* remove file's md5sum from database */
retvalue files_remove(struct database *database, const char *filekey, bool ignoremissing) {
	retvalue r;

	if( database->contents != NULL ) {
		(void)table_deleterecord(database->contents, filekey, true);
	}
	if( database->oldmd5sums != NULL ) {
		(void)table_deleterecord(database->checksums, filekey, true);
		r = table_deleterecord(database->oldmd5sums, filekey, true);
		if( r == RET_NOTHING && !ignoremissing ) {
			fprintf(stderr, "Unable to forget unknown filekey '%s'.\n",
					filekey);
			return RET_ERROR_MISSING;
		}
	} else {
		r = table_deleterecord(database->checksums, filekey, true);
		if( r == RET_NOTHING && !ignoremissing ) {
			fprintf(stderr, "Unable to forget unknown filekey '%s'.\n",
					filekey);
			return RET_ERROR_MISSING;
		}
	}
	return r;
}

/* delete the file and remove its md5sum from database */
retvalue files_deleteandremove(struct database *database, const char *filekey, bool rmdirs, bool ignoreifnot) {
	int err,en;
	char *filename;
	retvalue r;

	if( interrupted() )
		return RET_ERROR_INTERRUPTED;
	if( verbose >= 1 )
		printf("deleting and forgetting %s\n",filekey);
	filename = files_calcfullfilename(database, filekey);
	if( filename == NULL )
		return RET_ERROR_OOM;
	err = unlink(filename);
	if( err != 0 ) {
		en = errno;
		r = RET_ERRNO(en);
		if( errno == ENOENT ) {
			if( !ignoreifnot )
				fprintf(stderr,"%s not found, forgetting anyway\n",filename);
		} else {
			fprintf(stderr, "error %d while unlinking %s: %s\n",
					en, filename, strerror(en));
			free(filename);
			return r;
		}
	} else if(rmdirs) {
		/* try to delete parent directories, until one gives
		 * errors (hopefully because it still contains files) */
		size_t fixedpartlen = strlen(database->filesdir);
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
				if( verbose >= 1 ) {
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
	return files_remove(database, filekey, ignoreifnot);
}

/* hardlink file with known checksums and add it to database */
retvalue files_hardlinkandadd(struct database *database, const char *tempfile, const char *filekey, const struct checksums *checksums) {
	retvalue r;

	/* an additional check to make sure nothing tricks us into
	 * overwriting it by another file */
	r = files_canadd(database, filekey, checksums);
	if( !RET_IS_OK(r) )
		return r;
	r = checksums_hardlink(database->filesdir, filekey, tempfile, checksums);
	if( RET_WAS_ERROR(r) )
		return r;

	return files_add_checksums(database, filekey, checksums);
}

/* check if file is already there (RET_NOTHING) or could be added (RET_OK)
 * or RET_ERROR_WRONG_MD5SUM if filekey is already there with different md5sum */
retvalue files_canadd(struct database *database, const char *filekey, const struct checksums *checksums) {
	retvalue r;
	struct checksums *indatabase;
	bool improves;

	r = files_get_checksums(database, filekey, &indatabase);
	if( r == RET_NOTHING )
		return RET_OK;
	if( RET_WAS_ERROR(r) )
		return r;
	if( !checksums_check(indatabase, checksums, &improves) ) {
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
retvalue files_expect(struct database *database, const char *filekey, const struct checksums *checksums) {
	retvalue r;
	char *filename;
	struct checksums *filechecksums;
	bool improves;

	r = files_canadd(database, filekey, checksums);
	if( r == RET_NOTHING )
		return RET_OK;
	if( RET_WAS_ERROR(r) )
		return r;

	/* ready to add means missing, so have to look for the file itself: */
	filename = files_calcfullfilename(database, filekey);
	if( filename == NULL )
		return RET_ERROR_OOM;

	/* first check if a possible manually put (or left over from previous
	 * downloads attepts) file is there and has the correct file size */
	r = checksums_cheaptest(filename, checksums, false);
	if( r == RET_ERROR_WRONG_MD5) {
		fprintf(stderr,
"Deleting unexpected file '%s'!\n"
"(found in pool but not in database and file size is wrong.)\n ",
				filename);
		if( unlink(filename) == 0 )
			r = RET_NOTHING;
		else {
			int e = errno;
			fprintf(stderr,
"Error %d deleting '%s': %s!\n", e, filename, strerror(e));
		}
	}
	/* if it is, check its checksums */
	if( RET_IS_OK(r) )
		r = checksums_read(filename, &filechecksums);
	if( !RET_IS_OK(r) ) {
		free(filename);
		return r;
	}

	if( !checksums_check(checksums, filechecksums, &improves) ) {
		fprintf(stderr,
"Deleting unexpected file '%s'!\n"
"(found in pool but checksums are different from file to be added.)\n ",
				filename);
		if( unlink(filename) == 0 )
			r = RET_NOTHING;
		else {
			int e = errno;
			fprintf(stderr,
"Error %d deleting '%s': %s!\n", e, filename, strerror(e));
			r = RET_ERROR_WRONG_MD5;
		}
		checksums_free(filechecksums);
		free(filename);
		return r;
	}
	free(filename);

	// TODO: some callers might want the updated checksum when
	// improves is true, how to get them there?

	/* add found file to database */
	r = files_add_checksums(database, filekey, filechecksums);
	checksums_free(filechecksums);
	assert( r != RET_NOTHING );
	return r;
}

/* check for several files in the database and in the pool if missing */
retvalue files_expectfiles(struct database *database, const struct strlist *filekeys, struct checksums *checksumsarray[]) {
	int i;
	retvalue r;

	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *filekey = filekeys->values[i];
		const struct checksums *checksums = checksumsarray[i];

		r = files_expect(database, filekey, checksums);
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
		if( r == RET_NOTHING ) {
			/* File missing */
			fprintf(stderr,"Missing file %s\n",filekey);
			return RET_ERROR_MISSING;
		}
	}
	return RET_OK;
}

/* print missing files */
retvalue files_printmissing(struct database *database, const struct strlist *filekeys, const struct checksumsarray *origfiles) {
	int i;
	retvalue ret,r;

	ret = RET_NOTHING;
	assert( filekeys->count == origfiles->names.count );
	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *filekey = filekeys->values[i];
		const char *origfile = origfiles->names.values[i];
		const struct checksums *checksums = origfiles->checksums[i];

		r = files_expect(database, filekey, checksums);
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
		if( r == RET_NOTHING ) {
			/* File missing */
			(void)fputs(origfile, stdout);
			(void)putchar(' ');
			(void)fputs(database->filesdir, stdout);
			(void)putchar('/');
			(void)fputs(filekey, stdout);
			(void)putchar('\n');
			RET_UPDATE(ret,RET_OK);
		} else
			RET_UPDATE(ret,r);
	}
	return ret;
}

/* dump out all information */
retvalue files_printmd5sums(struct database *database) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *checksum;

	if( database->oldmd5sums == NULL ) {
		r = table_newglobalcursor(database->checksums, &cursor);
		if( !RET_IS_OK(r) )
			return r;
		result = RET_NOTHING;
		while( cursor_nexttemp(database->checksums, cursor,
					&filekey, &checksum) ) {
			result = RET_OK;
			(void)fputs(filekey, stdout);
			(void)putchar(' ');
			while( *checksum == ':' ) {
				while( *checksum != ' ' &&
				       *checksum != '\0' )
					checksum++;
				if( *checksum == ' ' )
					checksum++;
			}
			(void)fputs(checksum, stdout);
			(void)putchar('\n');
		}
		r = cursor_close(database->checksums, cursor);
		RET_ENDUPDATE(result, r);
		return result;
	}
	r = table_newglobalcursor(database->oldmd5sums, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( cursor_nexttemp(database->oldmd5sums, cursor,
				&filekey, &checksum) ) {
		result = RET_OK;
		(void)fputs(filekey, stdout);
		(void)putchar(' ');
		(void)fputs(checksum, stdout);
		(void)putchar('\n');
	}
	r = cursor_close(database->oldmd5sums, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue files_printchecksums(struct database *database) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *checksum, *md5sum;
	size_t md5sumlen, checksumlen;

	if( database->oldmd5sums == NULL ) {
		r = table_newglobalcursor(database->checksums, &cursor);
		if( !RET_IS_OK(r) )
			return r;
		result = RET_NOTHING;
		while( cursor_nexttemp(database->checksums, cursor,
					&filekey, &checksum) ) {
			result = RET_OK;
			(void)fputs(filekey, stdout);
			(void)putchar(' ');
			(void)fputs(checksum, stdout);
			(void)putchar('\n');
			if( interrupted() ) {
				result = RET_ERROR_INTERRUPTED;
				break;
			}
		}
		r = cursor_close(database->checksums, cursor);
		RET_ENDUPDATE(result, r);
		return result;
	}
	r = table_newglobalcursor(database->oldmd5sums, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( cursor_nexttempdata(database->oldmd5sums, cursor,
				&filekey, &md5sum, &md5sumlen) ) {
		result = RET_OK;
		(void)fputs(filekey, stdout);
		(void)putchar(' ');
		r = table_gettemprecord(database->checksums, filekey,
				&checksum, &checksumlen);
		if( r == RET_NOTHING || checksumlen <= md5sumlen ||
				strcmp(checksum + checksumlen - md5sumlen,
					md5sum) != 0 )
			(void)fputs(md5sum, stdout);
		else if( RET_IS_OK(r) )
			(void)fputs(checksum, stdout);
		RET_UPDATE(result, r);
		(void)putchar('\n');
		if( interrupted() ) {
			result = RET_ERROR_INTERRUPTED;
			break;
		}
	}
	r = cursor_close(database->oldmd5sums, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

/* callback for each registered file */
retvalue files_foreach(struct database *database,per_file_action action,void *privdata) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *checksum;

	if( database->oldmd5sums == NULL ) {
		r = table_newglobalcursor(database->checksums, &cursor);
		if( !RET_IS_OK(r) )
			return r;
		result = RET_NOTHING;
		while( cursor_nexttemp(database->checksums, cursor,
					&filekey, &checksum) ) {
			if( interrupted() ) {
				RET_UPDATE(result, RET_ERROR_INTERRUPTED);
				break;
			}
			r = action(privdata, filekey);
			RET_UPDATE(result, r);
		}
		r = cursor_close(database->checksums, cursor);
		RET_ENDUPDATE(result, r);
		return result;
	}
	r = table_newglobalcursor(database->oldmd5sums, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( cursor_nexttemp(database->oldmd5sums, cursor,
				&filekey, &checksum) ) {
		if( interrupted() ) {
			RET_UPDATE(result, RET_ERROR_INTERRUPTED);
			break;
		}
		r = action(privdata, filekey);
		RET_UPDATE(result,r);
	}
	r = cursor_close(database->oldmd5sums, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

static retvalue checkpoolfile(const char *fullfilename, const struct checksums *expected, bool *improveable) {
	struct checksums *actual;
	retvalue r;
	bool improves;

	r = checksums_read(fullfilename, &actual);
	if( RET_IS_OK(r) ) {
		if( !checksums_check(expected, actual, &improves) ) {
			fprintf(stderr, "WRONG CHECKSUMS of '%s':\n",
					fullfilename);
			checksums_printdifferences(stderr, expected, actual);
			r = RET_ERROR_WRONG_MD5;
		} else if( improves )
			*improveable = true;
		checksums_free(actual);
	}
	return r;
}

static retvalue leftoverchecksum(struct database *database, struct cursor *cursor2, const char *filekey2) {
	char *fullfilename;

	fullfilename = files_calcfullfilename(database, filekey2);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	if( isregularfile(fullfilename) ) {
		fprintf(stderr,
"WARNING: file '%s'\n"
"is listed in checksums.db but not the legacy (but still binding) files.db!\n"
"This should normaly only happen if information about the file was collected\n"
"by a version at least 3.3.0 and deleted by an earlier version. But then the\n"
"file should be deleted, too. But it seems to still be there. Strange.\n",
				filekey2);
	} else {
		static bool firstwarning = true;
		fprintf(stderr,
"deleting left over entry for file '%s'\n"
"listed in checksums.db but not the legacy (but still canonical) files.db!\n",
				filekey2);
		if( firstwarning ) {
			(void)fputs(
"This should only happen when information about it was collected with a\n"
"version of at least 3.3.0 of reprepro but deleted later with a earlier\n"
"version. But in that case it is normal.\n",			stderr);
			firstwarning = false;
		}
		(void)cursor_delete(database->checksums, cursor2, filekey2,
				NULL);
	}
	free(fullfilename);
	return RET_NOTHING;
}

retvalue files_checkpool(struct database *database, bool fast) {
	retvalue result, r;
	struct cursor *cursor, *cursor2;
	const char *filekey, *md5sum;
	const char *filekey2, *combined;
	size_t combinedlen;
	struct checksums *expected;
	char *fullfilename;
	bool improveable = false;

	result = RET_NOTHING;
	if( database->oldmd5sums == NULL ) {
		r = table_newglobalcursor(database->checksums, &cursor);
		if( !RET_IS_OK(r) )
			return r;
		while( cursor_nexttempdata(database->checksums, cursor,
					&filekey, &combined, &combinedlen) ) {
			r = checksums_setall(&expected,
					combined, combinedlen, NULL);
			if( RET_WAS_ERROR(r) ) {
				RET_UPDATE(result, r);
				continue;
			}
			fullfilename = files_calcfullfilename(database, filekey);
			if( fullfilename == NULL ) {
				result = RET_ERROR_OOM;
				checksums_free(expected);
				break;
			}
			if( fast )
				r = checksums_cheaptest(fullfilename, expected, true);
			else
				r = checkpoolfile(fullfilename, expected, &improveable);
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Missing file '%s'!\n", fullfilename);
				r = RET_ERROR_MISSING;
			}
			free(fullfilename);
			checksums_free(expected);
			RET_UPDATE(result,r);
		}
		r = cursor_close(database->checksums, cursor);
		RET_ENDUPDATE(result, r);
	} else {
		bool havemd5sums;
		int c IFSTUPIDCC(=-1);

		r = table_newglobalcursor(database->oldmd5sums, &cursor);
		if( RET_WAS_ERROR(r) )
			return r;
		havemd5sums = r != RET_NOTHING;
		r = table_newglobalcursor(database->checksums, &cursor2);
		if( RET_WAS_ERROR(r) ) {
			(void)cursor_close(database->oldmd5sums, cursor);
			return r;
		}
#define nextcombined() if( !cursor_nexttempdata(database->checksums, \
					cursor2, &filekey2, \
					&combined, &combinedlen) ) \
			filekey2 = NULL;
		if( r == RET_NOTHING )
			filekey2 = NULL;
		else
			nextcombined();
		while( havemd5sums && cursor_nexttemp(database->oldmd5sums,
					cursor, &filekey, &md5sum) ) {
			while( filekey2 != NULL &&
			       (c = strcmp(filekey2, filekey)) < 0 ) {
				r = leftoverchecksum(database, cursor2, filekey2);
				if( RET_WAS_ERROR(r) ) {
					result = r;
					break;
				}
				nextcombined();
			}
			if( filekey == NULL || c > 0 ) {
				r = checksums_parse(&expected, md5sum);
				improveable = true;
			} else {
				size_t md5sumlen = strlen(md5sum);
				if( combinedlen < md5sumlen || strcmp(md5sum,
				    combined + combinedlen - md5sumlen) != 0 ) {
					// TODO: instead check which of those
					// the actual files describes better.
					fprintf(stderr,
"WARNING: disparities between old-style files.db and new-style checksums.db\n"
"this should only happening if the file was removed and readded using an\n"
"old (pre-3.3) version of reprepro. Proceeding with the new-style checksum\n"
"of '%s' deleted.\n",					filekey);
					(void)cursor_delete(
							database->checksums,
							cursor2, filekey2, NULL);
					r = checksums_parse(&expected, md5sum);
					improveable = true;
				} else {
					r = checksums_setall(&expected,
						combined, combinedlen, NULL);
				}
				/* extended checksum processed. next! */
				nextcombined();
			}
			if( RET_WAS_ERROR(r) ) {
				RET_UPDATE(result, r);
				continue;
			}
			fullfilename = files_calcfullfilename(database, filekey);
			if( fullfilename == NULL ) {
				result = RET_ERROR_OOM;
				checksums_free(expected);
				break;
			}
			if( fast )
				r = checksums_cheaptest(fullfilename, expected, true);
			else
				r = checkpoolfile(fullfilename, expected, &improveable);
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Missing file '%s'!\n", fullfilename);
				r = RET_ERROR_MISSING;
			}
			free(fullfilename);
			checksums_free(expected);
			RET_UPDATE(result, r);
		}
		r = cursor_close(database->oldmd5sums, cursor);
		RET_ENDUPDATE(result, r);
		while( filekey2 != NULL ) {
			r = leftoverchecksum(database, cursor2, filekey2);
			RET_UPDATE(result, r);
			nextcombined();
		}
#undef nextcombined
		r = cursor_close(database->checksums, cursor2);
		RET_ENDUPDATE(result, r);
	}
	if( improveable && verbose >= 0 )
		printf(
"There were files with only some of the checksums this version of reprepro\n"
"can compute recorded. To add those run reprepro collectnewchecksums.\n");
	return result;
}

static bool checkmd5(const char *combined, size_t combinedlen, const char *md5sum) {
	size_t md5sumlen = strlen(md5sum);
	if( combinedlen < md5sumlen )
		return false;
	return strcmp(md5sum, combined + combinedlen - md5sumlen) == 0;
}

static retvalue collectnewchecksums(struct database *database, const char *filekey, const char *md5sum ) {
	const char *all;
	size_t alllen;
	char *fullfilename;
	struct checksums *expected, *real;
	retvalue r;
	bool improves;

	/* while the files.db is still the dominant source for backward
	 * compatibility, the other hases can only be found in checksums.db,
	 * if there are any. */
	r = table_gettemprecord(database->checksums, filekey, &all, &alllen);

	if( RET_IS_OK(r) )
		r = checksums_setall(&expected, all, alllen, NULL);
	if( RET_IS_OK(r) ) {
		if( checkmd5(all, alllen, md5sum) &&
				checksums_iscomplete(expected) ) {
			/* everything available, no inconsistencies */
			checksums_free(expected);
			return RET_NOTHING;
		}
	} else
		expected = NULL;

	/* if we get here, there are either hashes missing,
	 * or already out of sync. So best we look at the
	 * actual file to decide: */
	fullfilename = files_calcfullfilename(database, filekey);
	if( fullfilename == NULL ) {
		checksums_free(expected);
		return RET_ERROR_OOM;
	}
	r = checksums_read(fullfilename, &real);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing file '%s'!\n", fullfilename);
		r = RET_ERROR_MISSING;
	}
	free(fullfilename);
	if( RET_WAS_ERROR(r) ) {
		checksums_free(expected);
		return r;
	}
	if( expected == NULL ) {
		/* only old-style md5sum recorded yet */
		if( !checksums_matches(real, cs_md5sum, md5sum) ) {
			fprintf(stderr,
"ERROR: recorded checksums for '%s' are incomplete and\n"
"the existing file does not match the recorded md5sum!\n", filekey);
			return RET_ERROR_WRONG_MD5;
		}
		r = files_replace_checksums(database, filekey, real);
		checksums_free(real);
		return r;
	}
	if( checksums_check(expected, real, &improves) ) {
		if( !checksums_matches(real, cs_md5sum, md5sum) )
			/* checksums.db is correct but files.db not */
			fprintf(stderr,
"SERIOUS WARNING: pool file '%s'\n"
"has different checksums recorded in files.db and checksums.db and the\n"
"one in checksums.db matches the current file while the one in files.db\n"
"does not. For this to happen there must be something seriously went\n"
"wrong. I suggest comparing the output of _listchecksums with the files\n"
"actually found in the pool.\n",	filekey);
		if( improves ) {
			r = checksums_combine(&expected, real, NULL);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) ) {
				checksums_free(expected);
				checksums_free(real);
				return r;
			}
		}
		r = files_replace_checksums(database, filekey, expected);
		checksums_free(expected);
		checksums_free(real);
		return r;
	}
	if( checksums_matches(real, cs_md5sum, md5sum) ) {
		/* checksums.db is not the valid, but files.db is.
		 * This is a valid state, as older versions of reprepro only
		 * know about files.db and thus cannot update checksums.db. */
		static bool havewarned = false;
		if( verbose >= 0 ) {
			printf(
"Warning: pool file '%s' got outdated extended hashes.\n", filekey);
			if( ! havewarned ) {
				(void)puts(
"This can happen if you used pre-3.3 versions of reprepro in between.\n"
"This is anticipated and should cause no problems, as long as you rerun\n"
"collectnewchecksums before you switch to a future version no longer\n"
"supporting that compatiblity.");
				havewarned = true;
			}
		}
		r = files_replace_checksums(database, filekey, real);
		checksums_free(expected);
		checksums_free(real);
		return r;
	}
	fprintf(stderr, "ERROR: Cannot collect missing checksums for '%s'\n"
"as the file in the pool does not match the already recorded checksums\n",
				filekey);
	checksums_free(expected);
	checksums_free(real);
	return RET_ERROR_WRONG_MD5;
}

retvalue files_collectnewchecksums(struct database *database) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *all, *m;
	size_t alllen;
	struct checksums *expected;
	char *fullfilename;

	result = RET_NOTHING;
	if( database->oldmd5sums == NULL ) {
		r = table_newglobalcursor(database->checksums, &cursor);
		if( !RET_IS_OK(r) )
			return r;
		while( cursor_nexttempdata(database->checksums, cursor,
					&filekey, &all, &alllen) ) {
			r = checksums_setall(&expected,
					all, alllen, NULL);
			if( !RET_IS_OK(r) ) {
				RET_UPDATE(result, r);
				continue;
			}
			if( checksums_iscomplete(expected) ) {
				checksums_free(expected);
				continue;
			}

			fullfilename = files_calcfullfilename(database, filekey);
			if( fullfilename == NULL ) {
				result = RET_ERROR_OOM;
				checksums_free(expected);
				break;
			}
			r = checksums_complete(&expected, fullfilename, NULL);
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Missing file '%s'!\n", fullfilename);
				r = RET_ERROR_MISSING;
			}
			if( r == RET_ERROR_WRONG_MD5 ) {
				fprintf(stderr,
"ERROR: Cannot collect missing checksums for '%s'\n"
"as the file in the pool does not match the already recorded checksums\n",
						filekey);
			}
			free(fullfilename);
			if( RET_IS_OK(r) )
				r = files_replace_checksums(database,
						filekey, expected);
			checksums_free(expected);
			RET_UPDATE(result,r);
		}
		r = cursor_close(database->checksums, cursor);
		RET_ENDUPDATE(result, r);
	} else {
		r = table_newglobalcursor(database->oldmd5sums, &cursor);
		if( !RET_IS_OK(r) )
			return r;
		// TODO: also look for overlooked checksums items here...
		while( cursor_nexttemp(database->oldmd5sums, cursor,
					&filekey, &m) ) {
			r = collectnewchecksums(database, filekey, m);
			RET_UPDATE(result, r);
		}
		r = cursor_close(database->oldmd5sums, cursor);
		RET_ENDUPDATE(result, r);
	}
	return result;
}

retvalue files_detect(struct database *database, const char *filekey) {
	struct checksums *checksums;
	char *fullfilename;
	retvalue r;

	fullfilename = files_calcfullfilename(database, filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	r = checksums_read(fullfilename, &checksums);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Error opening '%s'!\n", fullfilename);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		free(fullfilename);
		return r;
	}
	if( verbose > 20 ) {
// TODO: readd?
//		fprintf(stderr,"Md5sum of '%s' is '%s'.\n",fullfilename,md5sum);
	}
	free(fullfilename);
	r = files_add_checksums(database, filekey, checksums);
	checksums_free(checksums);
	return r;
}

struct rfd { bool reread;
	/*@temp@*/struct database *database;
};

static retvalue regenerate_filelist(void *data, const char *filekey) {
	struct database *database = ((struct rfd*)data)->database;
	bool reread = ((struct rfd*)data)->reread;
	size_t l = strlen(filekey);
	char *debfilename;
	char *filelist;
	size_t fls;
	retvalue r;

	if( l <= 4 || memcmp(filekey+l-4, ".deb", 4) != 0 )
		return RET_NOTHING;

	if( !reread && !table_recordexists(database->contents, filekey) )
		return RET_NOTHING;

	debfilename = files_calcfullfilename(database, filekey);
	if( debfilename == NULL )
		return RET_ERROR_OOM;

	r = getfilelist(&filelist, &fls, debfilename);
	free(debfilename);
	if( RET_IS_OK(r) ) {
		if( verbose > 0 )
			(void)puts(filekey);
		if( verbose > 6 ) {
			const char *p = filelist;
			while( *p != '\0' ) {
				(void)putchar(' ');
				(void)puts(p);
				p += strlen(p)+1;
			}
		}
		r = table_adduniqsizedrecord(
				database->contents,
				filekey, filelist, fls,
				true, true);
		free(filelist);
	}
	return r;
}

retvalue files_regenerate_filelist(struct database *database, bool reread) {
	struct rfd d;

	d.database = database;
	d.reread = reread;
	return files_foreach(database, regenerate_filelist, &d);
}

/* Include a yet unknown file into the pool */
retvalue files_preinclude(struct database *database, const char *sourcefilename, const char *filekey, struct checksums **checksums_p, bool *newlyadded_p) {
	retvalue r;
	struct checksums *checksums, *realchecksums;
	bool improves;
	char *fullfilename;

	r = files_get_checksums(database, filekey, &checksums);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) ) {
		*newlyadded_p = false;

		r = checksums_read(sourcefilename, &realchecksums);
		if( r == RET_NOTHING )
			r = RET_ERROR_MISSING;
		if( RET_WAS_ERROR(r) ) {
			checksums_free(checksums);
			return r;
		}
		if( !checksums_check(checksums, realchecksums, &improves) ) {
			fprintf(stderr,
"ERROR: '%s' cannot be included as '%s'.\n"
"Already existing files can only be included again, if they are the same, but:\n",
				sourcefilename, filekey);
			checksums_printdifferences(stderr, checksums, realchecksums);
			checksums_free(checksums);
			checksums_free(realchecksums);
			return RET_ERROR_WRONG_MD5;
		}
		if( improves ) {
			r = checksums_combine(&checksums, realchecksums, NULL);
			if( RET_WAS_ERROR(r) ) {
				checksums_free(realchecksums);
				checksums_free(checksums);
				return r;
			}
			r = files_replace_checksums(database, filekey,
					checksums);
			if( RET_WAS_ERROR(r) ) {
				checksums_free(realchecksums);
				checksums_free(checksums);
				return r;
			}
		}
		checksums_free(realchecksums);
		// args, this breaks retvalue semantics!
		if( checksums_p != NULL )
			*checksums_p = checksums;
		else
			checksums_free(checksums);
		return RET_NOTHING;
	}
	assert( sourcefilename != NULL );
	fullfilename = files_calcfullfilename(database, filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	(void)dirs_make_parent(fullfilename);
	r = checksums_copyfile(fullfilename, sourcefilename, &checksums);
	if( r == RET_ERROR_EXIST ) {
		// TODO: deal with already existing files!
		fprintf(stderr, "File '%s' does already exist!\n", fullfilename);
	}
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Could not open '%s'!\n", sourcefilename);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		free(fullfilename);
		return r;
	}
	free(fullfilename);
	*newlyadded_p = true;

	r = files_add_checksums(database, filekey, checksums);
	if( RET_WAS_ERROR(r) ) {
		checksums_free(checksums);
		return r;
	}
	if( checksums_p != NULL )
		*checksums_p = checksums;
	else
		checksums_free(checksums);
	return RET_OK;
}

static retvalue checkimproveorinclude(struct database *database, const char *sourcedir, const char *basename, const char *filekey, struct checksums **checksums_p, bool *improving) {
	retvalue r;
	struct checksums *checksums = NULL;
	bool improves, copied = false;
	char *fullfilename = files_calcfullfilename(database, filekey);

	if( fullfilename == NULL )
		return RET_ERROR_OOM;

	if( checksums_iscomplete(*checksums_p) ) {
		r = checksums_cheaptest(fullfilename, *checksums_p, true);
		if( r != RET_NOTHING ) {
			free(fullfilename);
			return r;
		}
	} else {
		r = checksums_read(fullfilename, &checksums);
		if( RET_WAS_ERROR(r) ) {
			free(fullfilename);
			return r;
		}
	}
	if( r == RET_NOTHING ) {
		char *sourcefilename = calc_dirconcat(sourcedir, basename);

		if( sourcefilename == NULL ) {
			free(fullfilename);
			return RET_ERROR_OOM;
		}

		fprintf(stderr,
"WARNING: file %s was lost!\n"
"(i.e. found in the database, but not in the pool)\n"
"trying to compensate...\n",
				filekey);
		(void)dirs_make_parent(fullfilename);
		r = checksums_copyfile(fullfilename, sourcefilename,
				&checksums);
		if( r == RET_ERROR_EXIST ) {
			fprintf(stderr,
"File '%s' seems to be missing and existing at the same time!\n"
"To confused to continue...\n", 		fullfilename);
		}
		if( r == RET_NOTHING ) {
			fprintf(stderr, "Could not open '%s'!\n", sourcefilename);
			r = RET_ERROR_MISSING;
		}
		free(sourcefilename);
		if( RET_WAS_ERROR(r) ) {
			free(fullfilename);
			return r;
		}
		copied = true;
	}

	assert( checksums != NULL );

	if( !checksums_check(*checksums_p, checksums, &improves) ) {
		if( copied ) {
			deletefile(fullfilename);
			fprintf(stderr,
"ERROR: Unexpected content of file '%s/%s'!\n", sourcedir, basename);
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
	if( improves ) {
		r = checksums_combine(checksums_p, checksums, NULL);
		if( RET_IS_OK(r) )
			*improving = true;
	}
	checksums_free(checksums);
	free(fullfilename);
	return r;
}

retvalue files_checkincludefile(struct database *database, const char *sourcedir,const char *basename, const char *filekey, struct checksums **checksums_p, bool *newlyincluded_p) {
	char *sourcefilename, *fullfilename;
	struct checksums *checksums;
	retvalue r;
	bool improves;

	assert( *checksums_p != NULL );

	*newlyincluded_p = false;
	r = files_get_checksums(database, filekey, &checksums);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) ) {
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
		if( !checksums_check(checksums, *checksums_p, &improves) ) {
			fprintf(stderr,
"ERROR: '%s/%s' cannot be included as '%s'.\n"
"Already existing files can only be included again, if they are the same, but:\n",
				sourcedir, basename, filekey);
			checksums_printdifferences(stderr, checksums, *checksums_p);
			checksums_free(checksums);
			return RET_ERROR_WRONG_MD5;
		}
		r = RET_NOTHING;
		if( improves )
			r = checksums_combine(&checksums, *checksums_p, NULL);
		if( !RET_WAS_ERROR(r) )
			r = checkimproveorinclude(database, sourcedir,
				basename, filekey, &checksums, &improves);
		if( !RET_WAS_ERROR(r) && improves )
			r = files_replace_checksums(database, filekey,
					checksums);
		if( RET_IS_OK(r) )
			r = RET_NOTHING;
		/* return the combined checksum */
		checksums_free(*checksums_p);
		*checksums_p = checksums;
		return r;
	}

	assert( sourcedir != NULL );
	sourcefilename = calc_dirconcat(sourcedir, basename);
	if( sourcefilename == NULL )
		return RET_ERROR_OOM;

	fullfilename = files_calcfullfilename(database, filekey);
	if( fullfilename == NULL ) {
		free(sourcefilename);
		return RET_ERROR_OOM;
	}

	(void)dirs_make_parent(fullfilename);
	r = checksums_copyfile(fullfilename, sourcefilename, &checksums);
	if( r == RET_ERROR_EXIST ) {
		// TODO: deal with already existing files!
		fprintf(stderr, "File '%s' does already exist!\n", fullfilename);
	}
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Could not open '%s'!\n", sourcefilename);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		free(fullfilename);
		free(sourcefilename);
		return r;
	}
	if( !checksums_check(*checksums_p, checksums, &improves) ) {
		deletefile(fullfilename);
		fprintf(stderr, "ERROR: Unexpected content of file '%s'!\n", sourcefilename);
		checksums_printdifferences(stderr, *checksums_p, checksums);
		r = RET_ERROR_WRONG_MD5;
	} else
		*newlyincluded_p = true;
	free(sourcefilename);
	free(fullfilename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	if( improves ) {
		r = checksums_combine(checksums_p, checksums, NULL);
		checksums_free(checksums);
		if( RET_WAS_ERROR(r) )
			return r;
	} else
		checksums_free(checksums);

	return files_add_checksums(database, filekey, *checksums_p);
}
