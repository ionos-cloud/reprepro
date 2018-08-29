/*  This file is part of "reprepro"
 *  Copyright (C) 2005,2007,2008,2009,2016 Bernhard R. Link
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>

#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "database.h"
#include "target.h"
#include "exports.h"
#include "configparser.h"
#include "filecntl.h"
#include "hooks.h"
#include "package.h"

static const char *exportdescription(const struct exportmode *mode, char *buffer, size_t buffersize) {
	char *result = buffer;
	enum indexcompression ic;
	static const char* compression_names[ic_count] = {
		"uncompressed"
		,"gzipped"
#ifdef HAVE_LIBBZ2
		,"bzip2ed"
#endif
#ifdef HAVE_LIBLZMA
		,"xzed"
#endif
	};
	bool needcomma = false,
	     needellipsis = false;

	assert (buffersize > 50);
	*buffer++ = ' '; buffersize--;
	*buffer++ = '('; buffersize--;
	for (ic = ic_first ; ic < ic_count ; ic++) {
		if ((mode->compressions & IC_FLAG(ic)) != 0) {
			size_t l = strlen(compression_names[ic]);
			assert (buffersize > l+3);
			if (needcomma) {
				*buffer++ = ','; buffersize--;
			}
			memcpy(buffer, compression_names[ic], l);
			buffer += l; buffersize -= l;
			needcomma = true;
		}
	}
	/* should be long enough for the previous things in all cases */
	assert (buffersize > 10);
	if (mode->hooks.count > 0) {
		int i;

		if (needcomma) {
			*buffer++ = ','; buffersize--;
		}
		strcpy(buffer, "script: ");
		buffer += 8; buffersize -= 8;
		needcomma = false;

		for (i = 0 ; i < mode->hooks.count ; i++) {
			const char *hook = dirs_basename(mode->hooks.values[i]);
			size_t l = strlen(hook);

			if (buffersize < 6) {
				needellipsis = true;
				break;
			}
			if (needcomma) {
				*buffer++ = ','; buffersize--;
			}

			if (l > buffersize - 5) {
				memcpy(buffer, hook, buffersize-5);
				buffer += (buffersize-5);
				buffersize -= (buffersize-5);
				needellipsis = true;
				break;
			} else {
				memcpy(buffer, hook, l);
				buffer += l; buffersize -= l;
				assert (buffersize >= 2);
			}
			needcomma = true;
		}
	}
	if (needellipsis) {
		/* moveing backward here is easier than checking above */
		if (buffersize < 5) {
			buffer -= (5 - buffersize);
			buffersize = 5;
		}
		*buffer++ = '.'; buffersize--;
		*buffer++ = '.'; buffersize--;
		*buffer++ = '.'; buffersize--;
	}
	assert (buffersize >= 2);
	*buffer++ = ')'; buffersize--;
	*buffer = '\0';
	return result;
}

retvalue exportmode_init(/*@out@*/struct exportmode *mode, bool uncompressed, /*@null@*/const char *release, const char *indexfile) {
	strlist_init(&mode->hooks);
	mode->compressions = IC_FLAG(ic_gzip) | (uncompressed
			? IC_FLAG(ic_uncompressed) : 0);
	mode->filename = strdup(indexfile);
	if (FAILEDTOALLOC(mode->filename))
		return RET_ERROR_OOM;
	if (release == NULL)
		mode->release = NULL;
	else {
		mode->release = strdup(release);
		if (FAILEDTOALLOC(mode->release))
			return RET_ERROR_OOM;
	}
	return RET_OK;
}

// TODO: check for scripts in confdir early...
retvalue exportmode_set(struct exportmode *mode, struct configiterator *iter) {
	retvalue r;
	char *word;

	r = config_getword(iter, &word);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Error parsing %s, line %u, column %u: Unexpected end of field!\n"
"Filename to use for index files (Packages, Sources, ...) missing.\n",
			config_filename(iter),
			config_markerline(iter), config_markercolumn(iter));
		return RET_ERROR_MISSING;
	}
	assert (word[0] != '\0');

	if (word[0] == '.') {
		free(word);
		fprintf(stderr,
"Error parsing %s, line %u, column %u: filename for index files expected!\n",
			config_filename(iter),
			config_markerline(iter), config_markercolumn(iter));
		return RET_ERROR;
	}

	free(mode->release);
	mode->release = NULL;
	free(mode->filename);
	mode->filename = word;

	r = config_getword(iter, &word);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING)
		word = NULL;
	if (r != RET_NOTHING && word[0] != '.') {
		assert (word[0] != '\0');
		mode->release = word;
		r = config_getword(iter, &word);
		if (RET_WAS_ERROR(r))
			return r;
	}
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Error parsing %s, line %u, column %u: Unexpected end of field!\n"
"Compression identifiers ('.', '.gz' or '.bz2') missing.\n",
			config_filename(iter),
			config_markerline(iter), config_markercolumn(iter));
		return RET_ERROR;
	}
	if (word[0] != '.') {
		fprintf(stderr,
"Error parsing %s, line %u, column %u:\n"
"Compression extension ('.', '.gz' or '.bz2') expected.\n",
			config_filename(iter),
			config_markerline(iter), config_markercolumn(iter));
		free(word);
		return RET_ERROR;
	}
	mode->compressions = 0;
	while (r != RET_NOTHING && word[0] == '.') {
		if (word[1] == '\0')
			mode->compressions |= IC_FLAG(ic_uncompressed);
		else if (word[1] == 'g' && word[2] == 'z' &&
				word[3] == '\0')
			mode->compressions |= IC_FLAG(ic_gzip);
#ifdef HAVE_LIBBZ2
		else if (word[1] == 'b' && word[2] == 'z' && word[3] == '2' &&
				word[4] == '\0')
			mode->compressions |= IC_FLAG(ic_bzip2);
#endif
#ifdef HAVE_LIBLZMA
		else if (word[1] == 'x' && word[2] == 'z' &&word[3] == '\0')
			mode->compressions |= IC_FLAG(ic_xz);
#endif
		else {
			fprintf(stderr,
"Error parsing %s, line %u, column %u:\n"
"Unsupported compression extension '%s'!\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter),
					word);
			free(word);
			return RET_ERROR;
		}
		free(word);
		r = config_getword(iter, &word);
		if (RET_WAS_ERROR(r))
			return r;
	}
	while (r != RET_NOTHING) {
		if (word[0] == '.') {
			fprintf(stderr,
"Error parsing %s, line %u, column %u:\n"
"Scripts starting with dot are forbidden to avoid ambiguity ('%s')!\n"
"Try to put all compressions first and then all scripts to avoid this.\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter),
					word);
			free(word);
			return RET_ERROR;
		} else {
			char *fullfilename = configfile_expandname(word, word);
			if (FAILEDTOALLOC(fullfilename))
				return RET_ERROR_OOM;
			r = strlist_add(&mode->hooks, fullfilename);
			if (RET_WAS_ERROR(r))
				return r;
		}
		r = config_getword(iter, &word);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

static retvalue gotfilename(const char *relname, size_t l, struct release *release) {

	if (l > 12 && memcmp(relname+l-12, ".tobedeleted", 12) == 0) {
		char *filename;

		filename = strndup(relname, l - 12);
		if (FAILEDTOALLOC(filename))
			return RET_ERROR_OOM;
		return release_adddel(release, filename);

	} else if (l > 4 && memcmp(relname+(l-4), ".new", 4) == 0) {
		char *filename, *tmpfilename;

		filename = strndup(relname, l - 4);
		if (FAILEDTOALLOC(filename))
			return RET_ERROR_OOM;
		tmpfilename = strndup(relname, l);
		if (FAILEDTOALLOC(tmpfilename)) {
			free(filename);
			return RET_ERROR_OOM;
		}
		return release_addnew(release, tmpfilename, filename);
	} else if (l > 5 && memcmp(relname + (l-5), ".new.", 5) == 0) {
		char *filename, *tmpfilename;

		filename = strndup(relname, l-5);
		if (FAILEDTOALLOC(filename))
			return RET_ERROR_OOM;
		tmpfilename = strndup(relname, l-1);
		if (FAILEDTOALLOC(tmpfilename)) {
			free(filename);
			return RET_ERROR_OOM;
		}
		return release_addsilentnew(release, tmpfilename, filename);
	} else if (l > 5 && memcmp(relname + (l-5), ".keep", 5) == 0) {
		return RET_OK;
	} else {
		char *filename;

		filename = strndup(relname, l);
		if (FAILEDTOALLOC(filename))
			return RET_ERROR_OOM;
		return release_addold(release, filename);
	}
}

static retvalue callexporthook(/*@null@*/const char *hook, const char *relfilename, const char *mode, struct release *release) {
	pid_t f, c;
	int status;
	int io[2];
	char buffer[1000];
	int already = 0;

	if (hook == NULL)
		return RET_NOTHING;

	status = pipe(io);
	if (status < 0) {
		int e = errno;
		fprintf(stderr, "Error %d creating pipe: %s!\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}

	f = fork();
	if (f < 0) {
		int e = errno;
		(void)close(io[0]);
		(void)close(io[1]);
		fprintf(stderr, "Error %d while forking for exporthook: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}
	if (f == 0) {
		char *reltmpfilename;
		int e;

		if (dup2(io[1], 3) < 0) {
			e = errno;
			fprintf(stderr, "Error %d dup2'ing fd %d to 3: %s\n",
					e, io[1], strerror(e));
			exit(255);
		}
		/* "Doppelt haelt besser": */
		if (io[0] != 3)
			(void)close(io[0]);
		if (io[1] != 3)
			(void)close(io[1]);
		closefrom(4);
		/* backward compatibility */
		reltmpfilename = calc_addsuffix(relfilename, "new");
		if (reltmpfilename == NULL) {
			exit(255);
		}
		sethookenvironment(causingfile, NULL, NULL, NULL);
		(void)execl(hook, hook, release_dirofdist(release),
				reltmpfilename, relfilename, mode,
				ENDOFARGUMENTS);
		e = errno;
		fprintf(stderr, "Error %d while executing '%s': %s\n",
				e, hook, strerror(e));
		exit(255);
	}
	close(io[1]);
	markcloseonexec(io[0]);

	if (verbose > 6)
		printf("Called %s '%s' '%s.new' '%s' '%s'\n",
			hook, release_dirofdist(release),
			relfilename, relfilename, mode);
	/* read what comes from the client */
	while (true) {
		ssize_t r;
		int last, j;

		r = read(io[0], buffer + already, 999 - already);
		if (r < 0) {
			int e = errno;
			fprintf(stderr,
"Error %d reading from exporthook: %s!\n",
					e, strerror(e));
			break;
		}

		already += r;
		if (r == 0) {
			buffer[already] = '\0';
			already++;
		}
		last = 0;
		for (j = 0 ; j < already ; j++) {
			if (buffer[j] == '\n' || buffer[j] == '\0') {
				int next = j+1;
				int e = (j>0)?(j-1):j;
				retvalue ret;

				while (last < j && xisspace(buffer[last]))
						last++;
				if (last >= j) {
					last = next;
					continue;
				}
				while (xisspace(buffer[e])) {
					e--;
					assert (e >= last);
				}

				ret = gotfilename(buffer + last, e - last + 1,
						release);
				if (RET_WAS_ERROR(ret)) {
					(void)close(io[0]);
					return ret;
				}
				last = next;
			}
		}
		if (last > 0) {
			if (already > last)
				memmove(buffer, buffer + last, already - last);
			already -= last;
		}
		if (r == 0)
			break;
	}
	(void)close(io[0]);
	do {
		c = waitpid(f, &status, WUNTRACED);
		if (c < 0) {
			int e = errno;
			fprintf(stderr,
"Error %d while waiting for hook '%s' to finish: %s\n", e, hook, strerror(e));
			return RET_ERRNO(e);
		}
	} while (c != f);
	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) == 0) {
			if (verbose > 6)
				printf("Exporthook successfully returned!\n");
			return RET_OK;
		} else {
			fprintf(stderr,
"Exporthook failed with exitcode %d!\n",
					(int)WEXITSTATUS(status));
			return RET_ERROR;
		}
	} else if (WIFSIGNALED(status)) {
		fprintf(stderr, "Exporthook killed by signal %d!\n",
				(int)(WTERMSIG(status)));
		return RET_ERROR;
	} else {
		fprintf(stderr,
"Exporthook terminated abnormally. (status is %x)!\n",
				status);
		return RET_ERROR;
	}
}

retvalue export_target(const char *relativedir, struct target *target,  const struct exportmode *exportmode, struct release *release, bool onlyifmissing, bool snapshot) {
	retvalue r;
	struct filetorelease *file;
	const char *status;
	char *relfilename;
	char buffer[100];
	struct package_cursor iterator;

	relfilename = calc_dirconcat(relativedir, exportmode->filename);
	if (FAILEDTOALLOC(relfilename))
		return RET_ERROR_OOM;

	r = release_startfile(release, relfilename, exportmode->compressions,
		       onlyifmissing, &file);
	if (RET_WAS_ERROR(r)) {
		free(relfilename);
		return r;
	}
	if (RET_IS_OK(r)) {
		if (release_oldexists(file)) {
			if (verbose > 5)
				printf("  replacing '%s/%s'%s\n",
					release_dirofdist(release), relfilename,
					exportdescription(exportmode, buffer, 100));
			status = "change";
		} else {
			if (verbose > 5)
				printf("  creating '%s/%s'%s\n",
					release_dirofdist(release), relfilename,
					exportdescription(exportmode, buffer, 100));
			status = "new";
		}
		r = package_openiterator(target, READONLY, true, &iterator);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(file);
			free(relfilename);
			return r;
		}
		while (package_next(&iterator)) {
			if (iterator.current.controllen == 0)
				continue;
			(void)release_writedata(file, iterator.current.control,
					iterator.current.controllen);
			(void)release_writestring(file, "\n");
			if (iterator.current.control[iterator.current.controllen-1] != '\n')
				(void)release_writestring(file, "\n");
		}
		r = package_closeiterator(&iterator);
		if (RET_WAS_ERROR(r)) {
			release_abortfile(file);
			free(relfilename);
			return r;
		}
		r = release_finishfile(release, file);
		if (RET_WAS_ERROR(r)) {
			free(relfilename);
			return r;
		}
	} else {
		if (verbose > 9)
			printf("  keeping old '%s/%s'%s\n",
				release_dirofdist(release), relfilename,
				exportdescription(exportmode, buffer, 100));
		status = "old";
	}
	if (!snapshot) {
		int i;

		for (i = 0 ; i < exportmode->hooks.count ; i++) {
			const char *hook = exportmode->hooks.values[i];

			r = callexporthook(hook, relfilename, status, release);
			if (RET_WAS_ERROR(r)) {
				free(relfilename);
				return r;
			}
		}
	}
	free(relfilename);
	return RET_OK;
}

void exportmode_done(struct exportmode *mode) {
	assert (mode != NULL);
	free(mode->filename);
	strlist_done(&mode->hooks);
	free(mode->release);
}
