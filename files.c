/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007 Bernhard R. Link
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
	char *md5sum;
	retvalue r;

	r = table_getrecord(database->files, filekey, &md5sum);
	if( !RET_IS_OK(r) )
		return r;
	return checksums_set(checksums_p, md5sum);
}

retvalue files_add_checksums(struct database *database, const char *filekey, const struct checksums *checksums) {
	retvalue r;
	char *md5sum;

	r = checksums_get(checksums, cs_md5sum, &md5sum);
	assert( r != RET_NOTHING);
	if( !RET_IS_OK(r) )
		return r;
	r = table_adduniqrecord(database->files, filekey, md5sum);
	free(md5sum);
	return r;
}

/* remove file's md5sum from database */
retvalue files_remove(struct database *database, const char *filekey, bool ignoremissing) {
	retvalue r;

	if( database->contents != NULL ) {
		(void)table_deleterecord(database->contents, filekey, true);
	}
	r = table_deleterecord(database->files, filekey, true);
	if( r == RET_NOTHING && !ignoremissing ) {
		fprintf(stderr, "To be forgotten filekey '%s' was not known.\n",
				filekey);
		return RET_ERROR_MISSING;
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
			fprintf(stderr, "error while unlinking %s: %m(%d)\n",
					filename, en);
			free(filename);
			return r;
		}
	} else if(rmdirs) {
		/* try to delete parent directories, until one gives
		 * errors (hopefully because it still contains files) */
		size_t fixedpartlen = strlen(database->mirrordir);
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
					fprintf(stderr,"ignoring error trying to rmdir %s: %m(%d)\n",filename,en);
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
	r = checksums_hardlink(database->mirrordir, filekey, tempfile, checksums);
	if( RET_WAS_ERROR(r) )
		return r;

	return files_add_checksums(database, filekey, checksums);
}

/* check if file is already there (RET_NOTHING) or could be added (RET_OK)
 * or RET_ERROR_WRONG_MD5SUM if filekey is already there with different md5sum */
retvalue files_canadd(struct database *database, const char *filekey, const struct checksums *checksums) {
	retvalue r;
	const char *md5fromdatabase;

	r = table_gettemprecord(database->files, filekey,
			&md5fromdatabase, NULL);
	if( r == RET_NOTHING )
		return RET_OK;
	if( RET_WAS_ERROR(r) )
		return r;
	if( !checksums_matches(checksums, cs_md5sum, md5fromdatabase) ) {
		char *md5sum;

		r = checksums_get(checksums, cs_md5sum, &md5sum);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr,
"File \"%s\" is already registered with other md5sum!\n"
"(database:'%s')!\n",
					filekey, md5fromdatabase);
			return RET_ERROR_WRONG_MD5;
		}
		fprintf(stderr,
"File \"%s\" is already registered with other md5sum!\n"
"(expect: '%s', database:'%s')!\n",
				filekey, md5sum, md5fromdatabase);
		free(md5sum);
		return RET_ERROR_WRONG_MD5;

	}
	// TODO: check other checksums here...
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
	r = checksums_cheaptest(filename, checksums);
	if( r == RET_ERROR_WRONG_MD5) {
		fprintf(stderr,
"Deleting unexpected file '%s'!\n"
"(found in pool but not in the database, and file size is wrong.)\n ",
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
			fputs(origfile, stdout); putchar(' ');
			fputs(database->mirrordir, stdout); putchar('/');
			fputs(filekey, stdout); putchar('\n');
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

	r = table_newglobalcursor(database->files, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( cursor_nexttemp(database->files, cursor,
				&filekey, &checksum) ) {
		result = RET_OK;
		fputs(filekey, stdout);putchar(' ');
		fputs(checksum, stdout);putchar('\n');
	}
	r = cursor_close(database->files, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

/* callback for each registered file */
retvalue files_foreach(struct database *database,per_file_action action,void *privdata) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *checksum;

	r = table_newglobalcursor(database->files, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( cursor_nexttemp(database->files, cursor,
				&filekey, &checksum) ) {
		r = action(privdata, filekey, checksum);
		RET_UPDATE(result,r);
	}
	r = cursor_close(database->files, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

static retvalue checkpoolfilefast(const char *fullfilename, const struct checksums *expected) {
	retvalue r;
	struct stat s;
	int i, e;
	off_t expectedsize;

	r = checksums_getfilesize(expected, &expectedsize);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;
	i = stat(fullfilename, &s);
	if( i < 0 ) {
		e = errno;
		fprintf(stderr, "Error %d checking status of '%s': %s\n",
				e, fullfilename, strerror(e));
		return RET_ERROR_MISSING;
	} else if( !S_ISREG(s.st_mode)) {
		fprintf(stderr, "Not a regular file: '%s'\n", fullfilename);
		return RET_ERROR;
	} else if( s.st_size != expectedsize ) {
		fprintf(stderr,
				"WRONG SIZE of '%s': expected %lld found %lld\n",
				fullfilename,
				(long long)expectedsize,
				(long long)s.st_size);
		return RET_ERROR;
	} else
		return RET_OK;
}

static retvalue checkpoolfile(const char *fullfilename, const struct checksums *expected, bool *improveable) {
	struct checksums *actual;
	retvalue r;
	bool improves;

	r = checksums_read(fullfilename, &actual);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing file '%s'!\n", fullfilename);
		r = RET_ERROR_MISSING;
	} else if( RET_IS_OK(r) ) {
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

retvalue files_checkpool(struct database *database, bool fast) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *md5sumexpected;
	struct checksums *expected;
	char *fullfilename;
	bool improveable = false;

	r = table_newglobalcursor(database->files, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( cursor_nexttemp(database->files, cursor,
				&filekey, &md5sumexpected) ) {
		r = checksums_parse(&expected,  md5sumexpected);
		if( RET_WAS_ERROR(r) ) {
			RET_UPDATE(result,r);
			continue;
		}
		fullfilename = files_calcfullfilename(database, filekey);
		if( fullfilename == NULL ) {
			result = RET_ERROR_OOM;
			checksums_free(expected);
			break;
		}
		if( fast )
			r = checkpoolfilefast(fullfilename, expected);
		else
			r = checkpoolfile(fullfilename, expected, &improveable);
		free(fullfilename);
		checksums_free(expected);
		RET_UPDATE(result,r);
	}
	r = cursor_close(database->files, cursor);
	RET_ENDUPDATE(result, r);
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

retvalue files_regenerate_filelist(struct database *database, bool reread) {
	struct cursor *cursor;
	retvalue result,r;
	const char *filekey, *checksum;

	assert( database->contents != NULL );

	r = table_newglobalcursor(database->files, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( cursor_nexttemp(database->files, cursor,
				&filekey, &checksum) ) {
		size_t l = strlen(filekey);
		if( l > 4 && strcmp(filekey+l-4,".deb") == 0 ) {

			if( reread || !table_recordexists(
					database->contents, filekey) ) {
				char *debfilename;
				char *filelist;
				size_t fls;

				debfilename = files_calcfullfilename(database,
						filekey);
				if( debfilename == NULL ) {
					result = r;
					break;
				}

				r = getfilelist(&filelist, &fls, debfilename);
				free(debfilename);
				if( RET_IS_OK(r) ) {
					if( verbose > 0 )
						puts(filekey);
					if( verbose > 6 ) {
						const char *p = filelist;
						while( *p != '\0' ) {
							putchar(' ');
							puts(p);
							p += strlen(p)+1;
						}
					}
					r = table_adduniqsizedrecord(
							database->contents,
							filekey, filelist, fls,
							true, true);
					free(filelist);
				}
				RET_UPDATE(result,r);
				if( RET_WAS_ERROR(r) ) {
					break;
				}
			}
		}
	}
	r = cursor_close(database->files, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

/* Include a yet unknown file into the pool */
retvalue files_preinclude(struct database *database, const char *sourcefilename, const char *filekey, struct checksums **checksums_p) {
	retvalue r;
	struct checksums *checksums, *realchecksums;
	bool improves;
	char *fullfilename;

	r = files_get_checksums(database, filekey, &checksums);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) ) {
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
			r = checksums_combine(&checksums, realchecksums);
			checksums_free(realchecksums);
			if( RET_WAS_ERROR(r) ) {
				checksums_free(realchecksums);
				checksums_free(checksums);
				return r;
			}
			/* TODO:
			r = files_add_improved(database, filekey, checksums);
			if( RET_WAS_ERROR(r) ) {
				checksums_free(realchecksums);
				checksums_free(checksums);
				return r;
			}*/
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

retvalue files_checkincludefile(struct database *database, const char *sourcedir,const char *basename, const char *filekey, struct checksums **checksums_p) {
	char *sourcefilename, *fullfilename;
	struct checksums *checksums;
	retvalue r;
	bool improves;

	assert( *checksums_p != NULL );

	r = files_get_checksums(database, filekey, &checksums);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) ) {
		if( !checksums_check(checksums, *checksums_p, &improves) ) {
			fprintf(stderr,
"ERROR: '%s/%s' cannot be included as '%s'.\n"
"Already existing files can only be included again, if they are the same, but:\n",
				sourcedir, basename, filekey);
			checksums_printdifferences(stderr, checksums, *checksums_p);
			checksums_free(checksums);
			return RET_ERROR_WRONG_MD5;
		}
		if( improves ) {
			r = checksums_combine(&checksums, *checksums_p);
			if( RET_WAS_ERROR(r) ) {
				checksums_free(checksums);
				return r;
			}
			/* TODO:
			r = files_add_improved(database, filekey, checksums);
			if( RET_WAS_ERROR(r) ) {
				checksums_free(checksums);
				return r;
			}*/
		}
		checksums_free(checksums);
		return RET_NOTHING;
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
	if( !checksums_check(*checksums_p, checksums, &improves) ) {
		deletefile(fullfilename);
		fprintf(stderr, "ERROR: Unexpected content of file '%s'!\n", sourcefilename);
		checksums_printdifferences(stderr, *checksums_p, checksums);
		r = RET_ERROR_WRONG_MD5;
	}
	free(sourcefilename);
	free(fullfilename);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	if( improves ) {
		r = checksums_combine(checksums_p, checksums);
		checksums_free(checksums);
		if( RET_WAS_ERROR(r) )
			return r;
	} else
		checksums_free(checksums);

	return files_add_checksums(database, filekey, *checksums_p);
}
