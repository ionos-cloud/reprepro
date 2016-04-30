/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2007 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include "error.h"
#include "filecntl.h"
#include "readtextfile.h"
#include "debfile.h"
#include "chunks.h"

#ifdef HAVE_LIBARCHIVE
#error Why did this file got compiled instead of debfile.c?
#endif
// **********************************************************************
// * This is a very simple implementation calling ar and tar, which
// * is only used with --without-libarchive or when no libarchive was
// * found.
// **********************************************************************

static retvalue try_extractcontrol(char **control, const char *debfile, bool brokentar) {
	int pipe_1[2];
	int pipe_2[2];
	int ret;
	pid_t ar, tar, pid;
	int status;
	char *controlchunk;

	retvalue result, r;

	result = RET_OK;

	ret = pipe(pipe_1);
	if (ret < 0) {
		int e = errno;
		fprintf(stderr, "Error %d creating pipe: %s\n", e, strerror(e));
		return RET_ERRNO(e);
	}

	ret = pipe(pipe_2);
	if (ret < 0) {
		int e = errno;
		close(pipe_1[0]); close(pipe_1[1]);
		fprintf(stderr, "Error %d creating pipe: %s\n", e, strerror(e));
		return RET_ERRNO(e);
	}

	ar = fork();
	if (ar < 0) {
		int e = errno;
		fprintf(stderr, "Error %d forking: %s\n", e, strerror(e));
		result = RET_ERRNO(e);
		close(pipe_1[0]); close(pipe_1[1]);
		close(pipe_2[0]); close(pipe_2[1]);
		return result;
	}

	if (ar == 0) {
		int e;
		/* calling ar */
		if (dup2(pipe_1[1], 1) < 0)
			exit(255);
		close(pipe_1[0]); close(pipe_1[1]);
		close(pipe_2[0]); close(pipe_2[1]);
		//TODO without explicit path
		ret = execl("/usr/bin/ar",
				"ar", "p", debfile, "control.tar.gz",
				ENDOFARGUMENTS);
		e = errno;
		fprintf(stderr, "ar call failed with error %d: %s\n",
				e, strerror(e));
		exit(254);
	}

	tar = fork();
	if (tar < 0) {
		int e = errno;
		result = RET_ERRNO(e);
		fprintf(stderr, "Error %d forking: %s\n", e, strerror(e));
		close(pipe_1[0]); close(pipe_1[1]);
		close(pipe_2[0]); close(pipe_2[1]);
		tar = -1;
	} else if (tar == 0) {
		int e;
		/* calling tar */
		if (dup2(pipe_1[0], 0) < 0)
			exit(255);
		if (dup2(pipe_2[1], 1) < 0)
			exit(255);
		close(pipe_1[0]); close(pipe_1[1]);
		close(pipe_2[0]); close(pipe_2[1]);
		//TODO without explicit path
		execl("/bin/tar", "tar", "-xOzf", "-",
				brokentar?"control":"./control",
				ENDOFARGUMENTS);
		e = errno;
		fprintf(stderr, "tar call failed with error %d: %s\n",
				e, strerror(e));
		exit(254);

	}

	close(pipe_1[0]); close(pipe_1[1]);
	markcloseonexec(pipe_2[0]); close(pipe_2[1]);

	controlchunk = NULL;

	/* read data: */
	if (RET_IS_OK(result)) {
		size_t len, controllen;
		const char *afterchanges;

		r = readtextfilefd(pipe_2[0],
				brokentar?
"output from ar p <debfile> control.tar.gz | tar -xOzf - control":
"output from ar p <debfile> control.tar.gz | tar -xOzf - ./control",
				&controlchunk, &controllen);
		if (RET_IS_OK(r)) {
			len = chunk_extract(controlchunk,
					controlchunk, controllen,
					false, &afterchanges);
			if (len == 0)
				r = RET_NOTHING;
			if (*afterchanges != '\0') {
				fprintf(stderr,
"Unexpected empty line in control information within '%s'\n"
"(obtained via 'ar p %s control.tar.gz | tar -XOzf - %scontrol')\n",
						debfile, debfile,
						brokentar?"":"./");
				free(controlchunk);
				controlchunk = NULL;
				r = RET_ERROR;
			}
		}
		if (r == RET_NOTHING) {
			free(controlchunk);
			controlchunk = NULL;
			fprintf(stderr,
"No control information found in .deb!\n");
			/* only report error now,
			 * if we haven't try everything yet */
			if (brokentar)
				r = RET_ERROR_MISSING;
		}
		RET_UPDATE(result, r);

	}

	while (ar != -1 || tar != -1) {
		pid=wait(&status);
		if (pid < 0) {
			if (errno != EINTR)
				RET_UPDATE(result, RET_ERRNO(errno));
		} else {
			if (pid == ar) {
				ar = -1;
				if (!WIFEXITED(status)) {
					fprintf(stderr,
"Ar exited unnaturally!\n");
					result = RET_ERROR;
				} else if (WEXITSTATUS(status) != 0) {
					fprintf(stderr,
"Error from ar for '%s': %d\n", debfile, WEXITSTATUS(status));
					result = RET_ERROR;
				}
			} else if (pid == tar) {
				tar = -1;
				if (!WIFEXITED(status)) {
					fprintf(stderr,
"Tar exited unnaturally!\n");
					result = RET_ERROR;
				} else if (!brokentar && WEXITSTATUS(status) == 2) {
					if (RET_IS_OK(result))
						result = RET_NOTHING;
				} else if (WEXITSTATUS(status) != 0) {
					fprintf(stderr,
"Error from tar for control.tar.gz within '%s': %d\n",
							debfile,
							WEXITSTATUS(status));
					result = RET_ERROR;
				}
			} else {
				// WTH?
				fprintf(stderr,
"Who is %d, and why does this bother me?\n", (int)pid);
			}
		}

	}
	if (RET_IS_OK(result)) {
		if (controlchunk == NULL)
			/* we got not data but tar gave not error.. */
			return RET_ERROR_MISSING;
		else
			*control = controlchunk;
	} else
		free(controlchunk);
	return result;
}

retvalue extractcontrol(char **control, const char *debfile) {
	retvalue r;

	r = try_extractcontrol(control, debfile, false);
	if (r != RET_NOTHING)
		return r;
	/* perhaps the control.tar.gz is packaged by hand wrongly,
	 * try again: */
	r = try_extractcontrol(control, debfile, true);
	if (RET_IS_OK(r)) {
		fprintf(stderr,
"WARNING: '%s' contains a broken/unusual control.tar.gz.\n"
"reprepro was able to work around this but other tools or versions might not.\n",
				debfile);
	}
	assert (r != RET_NOTHING);
	return r;
}

retvalue getfilelist(/*@out@*/char **filelist, /*@out@*/size_t *size, const char *debfile) {
	fprintf(stderr,
"Extraction of file list without libarchive currently not implemented.\n");
	return RET_ERROR;
#if 0
	int pipe_1[2];
	int pipe_2[2];
	int ret;
	pid_t ar, tar, pid;
	int status;
	struct filelistcompressor c;
	size_t last = 0;
	retvalue result;

#error this still needs to be reimplemented...
	result = filelistcompressor_setup(&c);
	if (RET_WAS_ERROR(result))
		return result;

	result = RET_OK;

	ret = pipe(pipe_1);
	if (ret < 0) {
		int e = errno;
		fprintf(stderr, "Error %d creating pipe: %s\n", e, strerror(e));
		filelistcompressor_cancel(&c);
		return RET_ERRNO(e);
	}

	ret = pipe(pipe_2);
	if (ret < 0) {
		int e = errno;
		fprintf(stderr, "Error %d creating pipe: %s\n", e, strerror(e));
		close(pipe_1[0]); close(pipe_1[1]);
		filelistcompressor_cancel(&c);
		return RET_ERRNO(e);
	}

	ar = fork();
	if (ar < 0) {
		int e = errno;
		fprintf(stderr, "Error %d forking: %s\n", e, strerror(e));
		result = RET_ERRNO(e);
		close(pipe_1[0]); close(pipe_1[1]);
		close(pipe_2[0]); close(pipe_2[1]);
		filelistcompressor_cancel(&c);
		return result;
	}

	if (ar == 0) {
		int e;
		/* calling ar */
		if (dup2(pipe_1[1], 1) < 0)
			exit(255);
		close(pipe_1[0]); close(pipe_1[1]);
		close(pipe_2[0]); close(pipe_2[1]);
		//TODO without explicit path
		ret = execl("/usr/bin/ar",
				"ar", "p", debfile, "data.tar.gz",
				ENDOFARGUMENTS);
		e = errno;
		fprintf(stderr, "ar call failed with error %d: %s\n",
				e, strerror(e));
		exit(254);
	}

	tar = fork();
	if (tar < 0) {
		int e = errno;
		result = RET_ERRNO(e);
		fprintf(stderr, "Error %d forking: %s\n", e, strerror(e));
		close(pipe_1[0]); close(pipe_1[1]);
		close(pipe_2[0]); close(pipe_2[1]);
		tar = -1;
	} else if (tar == 0) {
		int e;
		/* calling tar */
		if (dup2(pipe_1[0], 0) < 0)
			exit(255);
		if (dup2(pipe_2[1], 1) < 0)
			exit(255);
		close(pipe_1[0]); close(pipe_1[1]);
		close(pipe_2[0]); close(pipe_2[1]);
		//TODO without explicit path
		execl("/bin/tar", "tar", "-tzf", "-", ENDOFARGUMENTS);
		e = errno;
		fprintf(stderr, "tar call failed with error %d: %s\n",
				e, strerror(e));
		exit(254);

	}

	close(pipe_1[0]); close(pipe_1[1]);
	close(pipe_2[1]);

	/* read data: */
	if (RET_IS_OK(result)) do {
		ssize_t bytes_read;
		size_t ignore;

		if (listsize <= len + 512) {
			char *n;

			listsize = len + 1024;
			n = realloc(list, listsize);
			if (FAILEDTOALLOC(n)) {
				result = RET_ERROR_OOM;
				break;
			}
			list = n;
		}

		ignore = 0;
		bytes_read = read(pipe_2[0], list+len, listsize-len-1);
		if (bytes_read < 0) {
			int e = errno;
			fprintf(stderr, "Error %d reading from pipe: %s\n",
					e, strerror(e));
			result = RET_ERRNO(e);
			break;
		} else if (bytes_read == 0)
			break;
		else while (bytes_read > 0) {
			if (list[len] == '\0') {
				fprintf(stderr,
"Unexpected NUL character from tar while getting file list from %s!\n", debfile);
				result = RET_ERROR;
				break;
			} else if (list[len] == '\n') {
				if (len > last+ignore && list[len-1] != '/') {
					list[len] = '\0';
					len++;
					bytes_read--;
					memmove(list+last, list+last+ignore,
						1+len-last-ignore);
					last = len-ignore;
				} else {
					len++;
					bytes_read--;
					ignore = len-last;
				}
			} else if (list[len] == '.' && len == last+ignore) {
				len++; ignore++;
				bytes_read--;
			} else if (list[len] == '/' && len == last+ignore) {
				len++; ignore++;
				bytes_read--;
			} else {
				len++;
				bytes_read--;
			}
		}
		if (ignore > 0) {
			if (len <= last+ignore)
				len = last;
			else {
				memmove(list+last, list+last+ignore,
						1+len-last-ignore);
				len -= ignore;
			}
		}
	} while (true);
	if (len != last) {
		fprintf(stderr,
"WARNING: unterminated output from tar pipe while extracting file list of %s\n", debfile);
		list[len] = '\0';
		fprintf(stderr, "The item '%s' might got lost.\n",
				list+last);
		result = RET_ERROR;
	} else {
		char *n = realloc(list, len+1);
		if (FAILEDTOALLOC(n))
			result = RET_ERROR_OOM;
		else {
			list = n;
			list[len] = '\0';
		}
	}
	close(pipe_2[0]);

	while (ar != -1 || tar != -1) {
		pid=wait(&status);
		if (pid < 0) {
			if (errno != EINTR)
				RET_UPDATE(result, RET_ERRNO(errno));
		} else {
			if (pid == ar) {
				ar = -1;
				if (!WIFEXITED(status) ||
						WEXITSTATUS(status) != 0) {
					fprintf(stderr,
"Error from ar for '%s': %d\n", debfile, WEXITSTATUS(status));
					result = RET_ERROR;
				}
			} else if (pid == tar) {
				tar = -1;
				if (!WIFEXITED(status) ||
						WEXITSTATUS(status) != 0) {
					fprintf(stderr,
"Error from tar for data.tar.gz within '%s': %d\n",
							debfile,
							WEXITSTATUS(status));
					result = RET_ERROR;
				}
			} else {
				// WTH?
				fprintf(stderr,
"Who is %d, and why does this bother me?\n", pid);
			}
		}
	}
	if (RET_IS_OK(result))
		return filelistcompressor_finish(&c, filelist);
	else
		filelistcompressor_cancel(&c);
	return result;
#endif
}
