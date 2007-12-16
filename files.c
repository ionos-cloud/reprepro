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
#include "md5sum.h"
#include "checksums.h"
#include "dirs.h"
#include "names.h"
#include "files.h"
#include "copyfile.h"
#include "ignore.h"
#include "filelist.h"
#include "debfile.h"
#include "database_p.h"

extern int verbose;

/* Add file's md5sum to database */
retvalue files_add(struct database *database,const char *filekey,const char *md5sum) {
	return table_adduniqrecord(database->files, filekey, md5sum);
}
static retvalue files_get(struct database *database,const char *filekey,/*@out@*/char **md5sum) {
	return table_getrecord(database->files, filekey, md5sum);
}

static retvalue files_get_checksums(struct database *database, const char *filekey, /*@out@*/struct checksums **checksums_p) {
	char *md5sum;
	retvalue r;

	r = table_getrecord(database->files, filekey, &md5sum);
	if( !RET_IS_OK(r) )
		return r;
	return checksums_set(checksums_p, md5sum);
}

static retvalue files_add_checksums(struct database *database, const char *filekey, const struct checksums *checksums) {
	retvalue r;
	char *md5sum;

	r = checksums_get(checksums, cs_md5sum, &md5sum);
	assert( r != RET_NOTHING);
	if( !RET_IS_OK(r) )
		return r;
	r = files_add(database, filekey, md5sum);
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

/* check if file is already there (RET_NOTHING) or could be added (RET_OK)
 * or RET_ERROR_WRONG_MD5SUM if filekey is already there with different md5sum */
retvalue files_ready(struct database *database,const char *filekey,const char *md5sum) {
	retvalue r;
	const char *md5fromdatabase;

	r = table_gettemprecord(database->files, filekey,
			&md5fromdatabase, NULL);
	if( r == RET_NOTHING )
		return RET_OK;
	if( RET_WAS_ERROR(r) )
		return r;
	if( strcmp(md5sum, md5fromdatabase) != 0 ) {
		fprintf(stderr,
"File \"%s\" is already registered with other md5sum!\n"
"(expect: '%s', database:'%s')!\n",
				filekey, md5sum, md5fromdatabase);
		return RET_ERROR_WRONG_MD5;
	}
	return RET_NOTHING;
}

/* hardlink file with known checksums and add it to database */
retvalue files_hardlinkandadd(struct database *database, const char *tempfile, const char *filekey, const struct checksums *checksums) {
	retvalue r;

	/* an additional check to make sure nothing tricks us into
	 * overwriting it by another file */
	r = files_canadd(database, filekey, checksums);
	if( !RET_IS_OK(r) )
		return r;
	r = copyfile_hardlink(database->mirrordir, filekey, tempfile, NULL);
	if( RET_WAS_ERROR(r) )
		return r;

	return files_add_checksums(database, filekey, checksums);
}

/* check for file in the database and if not found there, if it can be detected */
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
retvalue files_expect(struct database *database,const char *filekey,const char *md5sum) {
	retvalue r;
	char *filename;

	r = files_ready(database, filekey, md5sum);
	if( r == RET_NOTHING )
		return RET_OK;
	if( RET_WAS_ERROR(r) )
		return r;
	/* ready to add means missing, so have to look for the file itself: */

	filename = files_calcfullfilename(database, filekey);
	if( filename == NULL )
		return RET_ERROR_OOM;
	r = md5sum_ensure(filename, md5sum, true);
	free(filename);
	if( !RET_IS_OK(r) )
		return r;

	/* add found file to database */
	return files_add(database, filekey, md5sum);
}

/* check for several files in the database and in the pool if missing */
retvalue files_expectfiles(struct database *database,const struct strlist *filekeys,const struct strlist *md5sums) {
	int i;
	retvalue r;

	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *filekey = filekeys->values[i];
		const char *md5sum = md5sums->values[i];

		r = files_expect(database, filekey, md5sum);
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
retvalue files_printmissing(struct database *database,const struct strlist *filekeys,const struct strlist *md5sums,const struct strlist *origfiles) {
	int i;
	retvalue ret,r;

	ret = RET_NOTHING;
	for( i = 0 ; i < filekeys->count ; i++ ) {
		const char *filekey = filekeys->values[i];
		const char *md5sum = md5sums->values[i];
		const char *origfile = origfiles->values[i];

		r = files_expect(database, filekey, md5sum);
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

static retvalue getfilesize(/*@out@*/off_t *s,const char *md5sum) {
	const char *p;

	p = md5sum;
	while( *p != '\0' && !xisspace(*p) ) {
		p++;
	}
	if( *p != '\0' ) {
		while( *p != '\0' && xisspace(*p) )
			p++;
		if( *p != '\0' ) {
			*s = (off_t)atoll(p);
			return RET_OK;
		}
	}
	fprintf(stderr,"Strange md5sum as missing space: '%s'\n",md5sum);
	return RET_ERROR;
}

retvalue files_checkpool(struct database *database, bool fast) {
	retvalue result, r;
	struct cursor *cursor;
	const char *filekey, *md5sumexpected;
	char *fullfilename;

	r = table_newglobalcursor(database->files, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	result = RET_NOTHING;
	while( cursor_nexttemp(database->files, cursor,
				&filekey, &md5sumexpected) ) {
		fullfilename = files_calcfullfilename(database, filekey);
		if( fullfilename == NULL ) {
			result = RET_ERROR_OOM;
			break;
		}
		if( fast ) {
			struct stat s;
			int i;
			off_t expectedsize;

			r = getfilesize(&expectedsize, md5sumexpected);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) ) {
				free(fullfilename);
				RET_UPDATE(result, r);
				continue;
			}
			i = stat(fullfilename,&s);
			if( i < 0 ) {
				fprintf(stderr,
"Error checking status of '%s': %m\n", fullfilename);
				r = RET_ERROR_MISSING;
			} else {
				if( !S_ISREG(s.st_mode)) {
					fprintf(stderr,
"Not a regular file: '%s'\n",fullfilename);
					r = RET_ERROR;
				} else if( s.st_size != expectedsize ) {
					fprintf(stderr,
"WRONG SIZE of '%s': expected %lld(from '%s') found %lld\n",
						fullfilename,
						(long long)expectedsize,
						md5sumexpected,
						(long long)s.st_size);
					r = RET_ERROR;
				} else
					r = RET_OK;
			}
		} else {
			char *realmd5sum;

			r = md5sum_read(fullfilename, &realmd5sum);
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Missing file '%s'!\n",
						fullfilename);
				r = RET_ERROR_MISSING;
			} else if( RET_IS_OK(r) ) {
				if( strcmp(realmd5sum,md5sumexpected) != 0 ) {
					fprintf(stderr,
"WRONG MD5SUM of '%s': found '%s' expected '%s'\n",
						fullfilename, realmd5sum,
						md5sumexpected);
					r = RET_ERROR_WRONG_MD5;
				}
				free(realmd5sum);
			}
		}
		free(fullfilename);
		RET_UPDATE(result,r);
	}
	r = cursor_close(database->files, cursor);
	RET_ENDUPDATE(result, r);
	return result;
}

retvalue files_detect(struct database *database,const char *filekey) {
	char *md5sum;
	char *fullfilename;
	retvalue r;

	fullfilename = files_calcfullfilename(database, filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
	r = md5sum_read(fullfilename,&md5sum);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Error opening '%s'!\n",fullfilename);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		free(fullfilename);
		return r;
	}
	if( verbose > 20 ) {
		fprintf(stderr,"Md5sum of '%s' is '%s'.\n",fullfilename,md5sum);
	}
	free(fullfilename);
	r = files_add(database, filekey, md5sum);
	free(md5sum);
	return r;


}

/* Include a given file into the pool. */
retvalue files_include(struct database *database,const char *sourcefilename,const char *filekey, const char *md5sum, char **calculatedmd5sum, int delete) {
	retvalue r;
	char *md5sumfound;

	if( md5sum != NULL ) {
		r = files_expect(database, filekey, md5sum);
		if( RET_WAS_ERROR(r) )
			return r;
		if( RET_IS_OK(r) ) {
			if( delete >= D_MOVE ) {
				deletefile(sourcefilename);
			}
			if( calculatedmd5sum != NULL ) {
				char *n = strdup(md5sum);
				if( n == NULL )
					return RET_ERROR_OOM;
				*calculatedmd5sum = n;
			}
			return RET_OK;
		}
	} else {
		char *md5indatabase,*md5offile;

		r = files_get(database, filekey, &md5indatabase);
		if( RET_WAS_ERROR(r) )
			return r;
		if( RET_IS_OK(r) ) {
			if( delete == D_INPLACE ) {
				if( calculatedmd5sum != NULL )
					*calculatedmd5sum = md5indatabase;
				else
					free(md5indatabase);
				return RET_OK;
			}

			r = md5sum_read(sourcefilename, &md5offile);
			if( r == RET_NOTHING )
				r = RET_ERROR_MISSING;
			if( RET_WAS_ERROR(r) ) {
				free(md5indatabase);
				return r;
			}
			if( strcmp(md5indatabase,md5offile) != 0 ) {
				fprintf(stderr,"File \"%s\" is already registered with other md5sum!\n(file: '%s', database:'%s')!\n",filekey,md5offile,md5indatabase);
				free(md5offile);
				free(md5indatabase);
				return RET_ERROR_WRONG_MD5;
			} else {
				// The file has the md5sum we know already.
				if( delete >= D_MOVE ) {
					deletefile(sourcefilename);
				}
				if( calculatedmd5sum != NULL )
					*calculatedmd5sum = md5indatabase;
				else
					free(md5indatabase);
				free(md5offile);
				return RET_NOTHING;
			}
		}
	}
	if( sourcefilename == NULL ) {
		fprintf(stderr,"Unable to find %s/%s!\n",
				database->mirrordir, filekey);
		if( delete == D_INPLACE ) {
			fprintf(stderr,"Perhaps you forgot to give dpkg-buildpackage the -sa option,\n or you cound try --ignore=missingfile\n");
		}
		return RET_ERROR_MISSING;
	} if( delete == D_INPLACE ) {
		if( IGNORING("Looking around if it is elsewhere", "To look around harder, ",missingfile,"Unable to find %s/%s!\n", database->mirrordir, filekey))
			r = copyfile_copy(database->mirrordir, filekey,
					sourcefilename, md5sum, &md5sumfound);
		else
			r = RET_ERROR_MISSING;
		if( RET_WAS_ERROR(r) )
			return r;
	} else if( delete == D_COPY ) {
		r = copyfile_copy(database->mirrordir, filekey, sourcefilename,
				md5sum, &md5sumfound);
		if( RET_WAS_ERROR(r) )
			return r;
	} else {
		assert( delete >= D_MOVE );
		r = copyfile_move(database->mirrordir, filekey, sourcefilename,
				md5sum, &md5sumfound);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	r = files_add(database, filekey, md5sumfound);
	if( RET_WAS_ERROR(r) ) {
		free(md5sumfound);
		return r;
	}
	if( calculatedmd5sum != NULL )
		*calculatedmd5sum = md5sumfound;
	else
		free(md5sumfound);
	return RET_OK;
}

/* same as above, but use sourcedir/basename instead of sourcefilename */
retvalue files_includefile(struct database *database,const char *sourcedir,const char *basename, const char *filekey, const char *md5sum, char **calculatedmd5sum, int delete) {
	char *sourcefilename;
	retvalue r;

	if( sourcedir == NULL ) {
		assert(delete == D_INPLACE);
		sourcefilename = NULL;
	} else {
		sourcefilename = calc_dirconcat(sourcedir,basename);
		if( sourcefilename == NULL )
			return RET_ERROR_OOM;
	}
	r = files_include(database, sourcefilename, filekey, md5sum,
			calculatedmd5sum, delete);
	free(sourcefilename);
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
				checksums_free(checksums);
				return r;
			}
			/* TODO:
			r = files_add_improved(database, filekey, checksums);
			if( RET_WAS_ERROR(r) ) {
				checksums_free(checksums);
				return r;
			}*/
		} else
			checksums_free(realchecksums);
		// args, this breaks retvalue semantics!
		*checksums_p = checksums;
		return RET_NOTHING;
	}
	assert( sourcefilename != NULL );
	fullfilename = files_calcfullfilename(database, filekey);
	if( fullfilename == NULL )
		return RET_ERROR_OOM;
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
	*checksums_p = checksums;
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

