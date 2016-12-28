/*  This file is part of "reprepro"
 *  Copyright (C) 2009 Bernhard R. Link
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

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <assert.h>
#include "globals.h"
#include "error.h"
#include "mprintf.h"
#include "sha1.h"
#include "filecntl.h"
#include "rredpatch.h"
#include "time.h"

/* apt had a bug, http://bugs.debian.org/545694
 * to fail if a patch file only prepends text.
 * This if fixed in apt version 0.7.24,
 * so this workaround can be disabled when older apt
 * versions are no longer expected (i.e. sqeeze is oldstable) */
#define APT_545694_WORKAROUND

/* apt always wants to apply the last patch
 * (see http://bugs.debian.org/545699), so
 * always create an fake-empty patch last */
#define APT_545699_WORKAROUND

static int max_patch_count = 20;

static const struct option options[] = {
	{"version", no_argument, NULL, 'V'},
	{"help", no_argument, NULL, 'h'},
	{"debug", no_argument, NULL, 'D'},
	{"merge", no_argument, NULL, 'm'},
	{"max-patch-count", required_argument, NULL, 'N'},
	{"reprepro-hook", no_argument, NULL, 'R'},
	{"patch", no_argument, NULL, 'p'},
	{NULL, 0, NULL, 0}
};

static void usage(FILE *f) {
	fputs(
"rredtool: handle the restricted subset of ed patches\n"
"          as used by Debian {Packages,Sources}.diff files.\n"
"Syntax:\n"
"	rredtool <directory> <newfile> <oldfile> <mode>\n"
"	 update .diff directory (to be called from reprepro)\n"
"	rredtool --merge <patches..>\n"
"	 merge patches into one patch\n"
"	rredtool --patch <file> <patches..>\n"
"	 apply patches to file\n", f);
}

static const char tab[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

struct hash {
	char sha1[2*SHA1_DIGEST_SIZE+1];
	off_t len;
};
/* we only need sha1 sum and we need it a lot, so implement a "only sha1" */
static void finalize_sha1(struct SHA1_Context *context, off_t len, /*@out@*/struct hash *hash){
	char *sha1;
	unsigned char sha1buffer[SHA1_DIGEST_SIZE];
	int i;

	SHA1Final(context, sha1buffer);
	sha1 = hash->sha1;
	for (i = 0 ; i < SHA1_DIGEST_SIZE ; i++) {
		*(sha1++) = tab[sha1buffer[i] >> 4];
		*(sha1++) = tab[sha1buffer[i] & 0xF];
	}
	*sha1 = '\0';
	hash->len = len;
}

static retvalue gen_sha1sum(const char *fullfilename, /*@out@*/struct hash *hash) {
	struct SHA1_Context context;
	static const size_t bufsize = 16384;
	unsigned char *buffer = malloc(bufsize);
	ssize_t sizeread;
	int e, i;
	int infd;
	struct stat s;

	if (FAILEDTOALLOC(buffer))
		return RET_ERROR_OOM;

	SHA1Init(&context);

	infd = open(fullfilename, O_RDONLY);
	if (infd < 0) {
		e = errno;
		if ((e == EACCES || e == ENOENT) &&
				!isregularfile(fullfilename)) {
			free(buffer);
			return RET_NOTHING;
		}
		fprintf(stderr, "Error %d opening '%s': %s\n",
				e, fullfilename, strerror(e));
		free(buffer);
		return RET_ERRNO(e);
	}
	i = fstat(infd, &s);
	if (i != 0) {
		e = errno;
		fprintf(stderr, "Error %d getting information about '%s': %s\n",
				e, fullfilename, strerror(e));
		(void)close(infd);
		free(buffer);
		return RET_ERRNO(e);
	}
	do {
		sizeread = read(infd, buffer, bufsize);
		if (sizeread < 0) {
			e = errno;
			fprintf(stderr, "Error %d while reading %s: %s\n",
					e, fullfilename, strerror(e));
			free(buffer);
			(void)close(infd);
			return RET_ERRNO(e);
		}
		SHA1Update(&context, buffer, (size_t)sizeread);
	} while (sizeread > 0);
	free(buffer);
	i = close(infd);
	if (i != 0) {
		e = errno;
		fprintf(stderr, "Error %d reading %s: %s\n",
				e, fullfilename, strerror(e));
		return RET_ERRNO(e);
	}
	finalize_sha1(&context, s.st_size, hash);
	return RET_OK;
}

struct fileandhash {
	FILE *f;
	off_t len;
	struct SHA1_Context context;
};

static void hash_and_write(const void *data, size_t len, void *p) {
	struct fileandhash *fh = p;

	fwrite(data, len, 1, fh->f);
	SHA1Update(&fh->context, data, len);
	fh->len += len;
}

#define DATEFMT "%Y-%m-%d-%H%M.%S"
#define DATELEN (4 + 1 + 2 + 1 + 2 + 1 + 2 + 2 + 1 + 2)

static retvalue get_date_string(char *date, size_t max) {
	struct tm *tm;
	time_t current_time;
	size_t len;

	assert (max == DATELEN + 1);

	current_time = time(NULL);
	if (current_time == ((time_t) -1)) {
		int e = errno;
		fprintf(stderr, "rredtool: Error %d from time: %s\n",
				e, strerror(e));
		return RET_ERROR;
	}
	tm = gmtime(&current_time);
	if (tm == NULL) {
		int e = errno;
		fprintf(stderr, "rredtool: Error %d from gmtime: %s\n",
				e, strerror(e));
		return RET_ERROR;
	}
	errno = 0;
	len = strftime(date, max, DATEFMT, tm);
	if (len == 0 || len != DATELEN) {
		fprintf(stderr,
"rredtool: internal problem calling strftime!\n");
		return RET_ERROR;
	}
	return RET_OK;
}

static int create_temporary_file(void) {
	const char *tempdir;
	char *filename;
	int fd;

	tempdir = getenv("TMPDIR");
	if (tempdir == NULL)
		tempdir = getenv("TEMPDIR");
	if (tempdir == NULL)
		tempdir = "/tmp";
	filename = mprintf("%s/XXXXXX", tempdir);
	if (FAILEDTOALLOC(filename)) {
		errno = ENOMEM;
		return -1;
	}
#ifdef HAVE_MKOSTEMP
	fd = mkostemp(filename, 0600);
#else
#ifdef HAVE_MKSTEMP
	fd = mkstemp(filename);
#else
#error Need mkostemp or mkstemp
#endif
#endif
	if (fd >= 0)
		unlink(filename);
	free(filename);
	return fd;
}

static retvalue execute_into_file(const char * const argv[], /*@out@*/int *fd_p, int expected_exit_code) {
	pid_t child, pid;
	int fd, status;

	fd = create_temporary_file();
	if (fd < 0) {
		int e = errno;
		fprintf(stderr, "Error %d creating temporary file: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}

	child = fork();
	if (child == (pid_t)-1) {
		int e = errno;
		fprintf(stderr, "rredtool: Error %d forking: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}
	if (child == 0) {
		int e, i;

		do {
			i = dup2(fd, 1);
			e = errno;
		} while (i < 0 && (e == EINTR || e == EBUSY));
		if (i < 0) {
			fprintf(stderr,
"rredtool: Error %d in dup2(%d, 0): %s\n",
					e, fd, strerror(e));
			raise(SIGUSR1);
			exit(EXIT_FAILURE);
		}
		close(fd);
		closefrom(3);
		execvp(argv[0], (char * const *)argv);
		fprintf(stderr, "rredtool: Error %d executing %s: %s\n",
				e, argv[0], strerror(e));
		raise(SIGUSR1);
		exit(EXIT_FAILURE);
	}
	do {
		pid = waitpid(child, &status, 0);
	} while (pid == (pid_t)-1 && errno == EINTR);
	if (pid == (pid_t)-1) {
		int e = errno;
		fprintf(stderr,
"rredtool: Error %d waiting for %s child %lu: %s!\n",
				e, argv[0], (unsigned long)child, strerror(e));
		(void)close(fd);
		return RET_ERROR;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == expected_exit_code) {
		if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
			int e = errno;
			fprintf(stderr,
"rredtool: Error %d rewinding temporary file to start: %s!\n",
					e, strerror(e));
			(void)close(fd);
			return RET_ERROR;
		}
		*fd_p = fd;
		return RET_OK;
	}
	close(fd);
	if (WIFEXITED(status)) {
		fprintf(stderr,
"rredtool: %s returned with unexpected exit code %d\n",
				argv[0], (int)(WEXITSTATUS(status)));
		return RET_ERROR;
	}
	if (WIFSIGNALED(status)) {
		if (WTERMSIG(status) != SIGUSR1)
			fprintf(stderr, "rredtool: %s killed by signal %d\n",
					argv[0], (int)(WTERMSIG(status)));
		return RET_ERROR;
	}
	fprintf(stderr, "rredtool: %s child dies mysteriously (status=%d)\n",
			argv[0], status);
	return RET_ERROR;
}

struct old_index_file {
	struct old_patch {
		struct old_patch *next, *prev;
		char *basefilename;
		/* part until the + in the name */
		char *nameprefix;
		struct hash hash;
	} *first, *last;
	struct hash hash;
};

static void old_index_done(/*@only@*/struct old_index_file *o) {
	while (o->first != NULL) {
		struct old_patch *p = o->first;

		o->first = p->next;
		free(p->basefilename);
		free(p->nameprefix);
		free(p);
	}
	o->last = NULL;
}

static retvalue make_prefix_uniq(struct old_patch *o) {
	struct old_patch *p, *last = NULL;
	const char *lookfor = o->nameprefix;

	/* make the prefix uniq by extending all previous occurrences
	 * of this prefix with an additional +. As this might already
	 * have happened, this has to be possibly repeated */

	while (true) {
		for (p = o->prev ; p != NULL ; p = p->prev) {
			if (p == last)
				continue;
			if (strcmp(p->nameprefix, lookfor) == 0) {
				char *h;
				size_t l = strlen(p->nameprefix);

				h = realloc(p->nameprefix, l+2);
				if (FAILEDTOALLOC(h))
					return RET_ERROR_OOM;
				h[l] = '+' ;
				h[l+1] = '\0';
				p->nameprefix = h;
				lookfor = h;
				last = p;
				break;
			}
		}
		if (p == NULL)
			return RET_OK;
	}
}

static inline retvalue parse_old_index(char *p, size_t len, struct old_index_file *oldindex) {
	char *q, *e = p + len;
	off_t filesize;
	struct old_patch *o;
	retvalue r;

	/* This is only supposed to parse files it wrote itself
	 * (otherwise not having merged patches would most likely break
	 * things in ugly ways), so parsing it can be very strict and easy: */

#define checkorfail(val) if (e - p < (intptr_t)strlen(val) || memcmp(p, val, strlen(val)) != 0) return RET_NOTHING; else { p += strlen(val); }

	checkorfail("SHA1-Current: ");
	q = strchr(p, '\n');
	if (q != NULL && q - p > 2 * SHA1_DIGEST_SIZE)
		q = memchr(p, ' ', q - p);
	if (q == NULL || q - p != 2 * SHA1_DIGEST_SIZE)
		return RET_NOTHING;
	memcpy(oldindex->hash.sha1, p, 2 * SHA1_DIGEST_SIZE);
	oldindex->hash.sha1[2 * SHA1_DIGEST_SIZE] = '\0';
	p = q;
	if (*p == ' ') {
		p++;
		filesize = 0;
		while (*p >= '0' && *p <= '9') {
			filesize = 10 * filesize + (*p - '0');
			p++;
		}
		oldindex->hash.len = filesize;
	} else
		oldindex->hash.len = (off_t)-1;
	checkorfail("\nSHA1-History:\n");
	while (*p == ' ') {
		p++;

		q = p;
		while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) {
			p++;
		}
		if (p - q != 2 * SHA1_DIGEST_SIZE)
			return RET_NOTHING;

		o = zNEW(struct old_patch);
		if (FAILEDTOALLOC(o))
			return RET_ERROR_OOM;
		o->prev = oldindex->last;
		oldindex->last = o;
		if (o->prev == NULL)
			oldindex->first = o;
		else
			o->prev->next = o;

		memcpy(o->hash.sha1, q, 2 * SHA1_DIGEST_SIZE);

		while (*p == ' ')
			p++;
		if (*p < '0' || *p > '9')
			return RET_NOTHING;
		filesize = 0;
		while (*p >= '0' && *p <= '9') {
			filesize = 10 * filesize + (*p - '0');
			p++;
		}
		o->hash.len = filesize;
		if (*p != ' ')
			return RET_NOTHING;
		p++;
		q = strchr(p, '\n');
		if (q == NULL)
			return RET_NOTHING;
		o->basefilename = strndup(p, (size_t)(q-p));
		if (FAILEDTOALLOC(o->basefilename))
			return RET_ERROR_OOM;
		p = q + 1;
		q = strchr(o->basefilename, '+');
		if (q == NULL)
			o->nameprefix = mprintf("%s+", o->basefilename);
		else
			o->nameprefix = strndup(o->basefilename,
					1 + (size_t)(q - o->basefilename));
		if (FAILEDTOALLOC(o->nameprefix))
			return RET_ERROR_OOM;
		r = make_prefix_uniq(o);
		if (RET_WAS_ERROR(r))
			return r;

		/* allow pseudo-empty fake patches */
		if (memcmp(o->hash.sha1, oldindex->hash.sha1,
					2 * SHA1_DIGEST_SIZE) == 0)
			continue;
		// TODO: verify filename and create prefix...
	}
	checkorfail("SHA1-Patches:\n");
	o = oldindex->first;
	while (*p == ' ') {
		p++;

		if (o == NULL)
			return RET_NOTHING;

		q = p;
		while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) {
			p++;
		}
		if (p - q != 2 * SHA1_DIGEST_SIZE)
			return RET_NOTHING;

		while (*p == ' ')
			p++;
		if (*p < '0' || *p > '9')
			return RET_NOTHING;
		while (*p >= '0' && *p <= '9') {
			p++;
		}
		if (*p != ' ')
			return RET_NOTHING;
		p++;
		q = strchr(p, '\n');
		if (q == NULL)
			return RET_NOTHING;
		if (strncmp(o->basefilename, p, (size_t)(q-p)) != 0
				|| o->basefilename[q-p] != '\0')
			return RET_NOTHING;
		p = q + 1;
		o = o->next;
	}
	checkorfail("X-Patch-Precedence: merged\n");
	if (*p != '\0' || p != e)
		return RET_NOTHING;
	// TODO: check for dangerous stuff (like ../ in basename)
	// TODO: ignore patches where the filename is missing?
	return RET_OK;
#undef checkorfail
}

static retvalue read_old_index(const char *fullfilename, /*@out@*/struct old_index_file *oldindex) {
	int fd, i;
	char *buffer;
	size_t buffersize = 102400, available = 0;
	ssize_t bytes_read;
	retvalue r;

	setzero(struct old_index_file, oldindex);

	if (!isregularfile(fullfilename))
		return RET_NOTHING;

	fd = open(fullfilename, O_RDONLY);
	if (fd < 0) {
		int e = errno;

		fprintf(stderr, "rredtool: Error %d opening '%s': %s\n",
				e, fullfilename, strerror(e));
		return RET_ERRNO(e);
	}

	/* index file should not be that big, so read into memory as a whole */
	buffer = malloc(buffersize);
	if (FAILEDTOALLOC(buffer)) {
		close(fd);
		return RET_ERROR_OOM;
	}
	do {
		bytes_read = read(fd, buffer + available,
				buffersize - available - 1);
		if (bytes_read < 0) {
			int e = errno;

			fprintf(stderr, "rredtool: Error %d reading '%s': %s\n",
					e, fullfilename, strerror(e));
			(void)close(fd);
			free(buffer);
			return RET_ERRNO(e);
		}
		assert ((size_t)bytes_read < buffersize - available);
		available += bytes_read;
		if (available + 1 >= buffersize) {
			fprintf(stderr,
"rredtool: Ridicilous long '%s' file!\n",
					fullfilename);
			(void)close(fd);
			free(buffer);
			return RET_ERROR;
		}
	} while (bytes_read > 0);
	i = close(fd);
	if (i != 0) {
		int e = errno;

		fprintf(stderr, "rredtool: Error %d reading '%s': %s\n",
				e, fullfilename, strerror(e));
		free(buffer);
		return RET_ERRNO(e);
	}
	buffer[available] = '\0';

	r = parse_old_index(buffer, available, oldindex);
	free(buffer);
	if (r == RET_NOTHING) {
		/* wrong format, most likely a left over file */
		fprintf(stderr,
"rredtool: File '%s' does not look like created by rredtool, ignoring!\n",
				fullfilename);
		old_index_done(oldindex);
		setzero(struct old_index_file, oldindex);
		return RET_NOTHING;
	}
	if (RET_WAS_ERROR(r)) {
		old_index_done(oldindex);
		setzero(struct old_index_file, oldindex);
		return r;
	}
	return RET_OK;
}

struct patch {
	struct patch *next;
	char *basefilename;
	size_t basefilename_len;
	char *fullfilename;
	struct hash hash, from;
};

static void patches_free(struct patch *r) {
	while (r != NULL) {
		struct patch *n = r->next;

		free(r->basefilename);
		if (r->fullfilename != NULL) {
			(void)unlink(r->fullfilename);
			free(r->fullfilename);
		}
		free(r);
		r = n;
	}
}

static retvalue new_diff_file(struct patch **root_p, const char *directory, const char *relfilename, const char *since, const char date[DATELEN+1], struct modification *r) {
	struct patch *p;
	int i, status, fd, pipefds[2], tries = 3;
	pid_t child, pid;
	retvalue result;
	struct fileandhash fh;

	p = zNEW(struct patch);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;

	if (since == NULL)
		since = "";
	p->basefilename = mprintf("%s%s", since, date);
	if (FAILEDTOALLOC(p->basefilename)) {
		patches_free(p);
		return RET_ERROR_OOM;
	}
	p->basefilename_len = strlen(p->basefilename);
	p->fullfilename = mprintf("%s/%s.diff/%s.gz.new",
			directory, relfilename, p->basefilename);
	if (FAILEDTOALLOC(p->fullfilename)) {
		patches_free(p);
		return RET_ERROR_OOM;
	}
	/* create the file */
	while (tries-- > 0) {
		int e;

		fd = open(p->fullfilename, O_CREAT|O_EXCL|O_NOCTTY|O_WRONLY, 0666);
		if (fd >= 0)
			break;
		e = errno;
		if (e == EEXIST && tries > 0)
			unlink(p->fullfilename);
		else {
			fprintf(stderr,
"rredtool: Error %d creating '%s': %s\n",
					e, p->fullfilename, strerror(e));
			return RET_ERROR;
		}
	}
	assert (fd > 0);
	/* start an child to compress connected via a pipe */
	i = pipe(pipefds);
	assert (pipefds[0] > 0);
	if (i != 0) {
		int e = errno;
		fprintf(stderr, "rredtool: Error %d creating pipe: %s\n",
				e, strerror(e));
		unlink(p->fullfilename);
		return RET_ERROR;
	}
	child = fork();
	if (child == (pid_t)-1) {
		int e = errno;
		fprintf(stderr, "rredtool: Error %d forking: %s\n",
				e, strerror(e));
		unlink(p->fullfilename);
		return RET_ERROR;
	}
	if (child == 0) {
		int e;

		close(pipefds[1]);
		do {
			i = dup2(pipefds[0], 0);
			e = errno;
		} while (i < 0 && (e == EINTR || e == EBUSY));
		if (i < 0) {
			fprintf(stderr,
"rredtool: Error %d in dup2(%d, 0): %s\n",
					e, pipefds[0], strerror(e));
			raise(SIGUSR1);
			exit(EXIT_FAILURE);
		}
		do {
			i = dup2(fd, 1);
			e = errno;
		} while (i < 0 && (e == EINTR || e == EBUSY));
		if (i < 0) {
			fprintf(stderr,
"rredtool: Error %d in dup2(%d, 0): %s\n",
					e, fd, strerror(e));
			raise(SIGUSR1);
			exit(EXIT_FAILURE);
		}
		close(pipefds[0]);
		close(fd);
		closefrom(3);
		execlp("gzip", "gzip", "-9", (char*)NULL);
		fprintf(stderr, "rredtool: Error %d executing gzip: %s\n",
				e, strerror(e));
		raise(SIGUSR1);
		exit(EXIT_FAILURE);
	}
	close(pipefds[0]);
	close(fd);
	/* send the data to the child */
	fh.f = fdopen(pipefds[1], "w");
	if (fh.f == NULL) {
		int e = errno;
		fprintf(stderr,
"rredtool: Error %d fdopen'ing write end of pipe to compressor: %s\n",
				e, strerror(e));
		close(pipefds[1]);
		unlink(p->fullfilename);
		patches_free(p);
		kill(child, SIGTERM);
		waitpid(child, NULL, 0);
		return RET_ERROR;
	}
	SHA1Init(&fh.context);
	fh.len = 0;
	modification_printaspatch(&fh, r, hash_and_write);
	result = RET_OK;
	i = ferror(fh.f);
	if (i != 0) {
		fprintf(stderr, "rredtool: Error sending data to gzip!\n");
		(void)fclose(fh.f);
		result = RET_ERROR;
	} else {
		i = fclose(fh.f);
		if (i != 0) {
			int e = errno;
			fprintf(stderr,
"rredtool: Error %d sending data to gzip: %s!\n",
					e, strerror(e));
			result = RET_ERROR;
		}
	}
	do {
		pid = waitpid(child, &status, 0);
	} while (pid == (pid_t)-1 && errno == EINTR);
	if (pid == (pid_t)-1) {
		int e = errno;
		fprintf(stderr,
"rredtool: Error %d waiting for gzip child %lu: %s!\n",
				e, (unsigned long)child, strerror(e));
		return RET_ERROR;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		if (RET_IS_OK(result)) {
			finalize_sha1(&fh.context, fh.len, &p->hash);
			p->next = *root_p;
			*root_p = p;
		}
		return result;
	}
	unlink(p->fullfilename);
	patches_free(p);
	if (WIFEXITED(status)) {
		fprintf(stderr,
"rredtool: gzip returned with non-zero exit code %d\n",
				(int)(WEXITSTATUS(status)));
		return RET_ERROR;
	}
	if (WIFSIGNALED(status)) {
		fprintf(stderr, "rredtool: gzip killed by signal %d\n",
				(int)(WTERMSIG(status)));
		return RET_ERROR;
	}
	fprintf(stderr, "rredtool: gzip child dies mysteriously (status=%d)\n",
			status);
	return RET_ERROR;
}

static retvalue write_new_index(const char *newindexfilename, const struct hash *newhash, const struct patch *root) {
	int tries, fd, i;
	const struct patch *p;

	tries = 2;
	while (tries > 0) {
		errno = 0;
		fd = open(newindexfilename,
				O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
		if (fd >= 0)
			break;
		if (errno == EINTR)
			continue;
		tries--;
		if (errno != EEXIST)
			break;
		unlink(newindexfilename);
	}
	if (fd < 0) {
		int e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, newindexfilename, strerror(e));
		return RET_ERROR;
	}
	i = dprintf(fd, "SHA1-Current: %s %lld\n" "SHA1-History:\n",
			newhash->sha1, (long long)newhash->len);
	for (p = root ; i >= 0 && p != NULL ; p = p->next) {
		i = dprintf(fd, " %s %7ld %s\n",
				p->from.sha1, (long int)p->from.len,
				p->basefilename);
	}
	if (i >= 0)
		i = dprintf(fd, "SHA1-Patches:\n");
	for (p = root ; i >= 0 && p != NULL ; p = p->next) {
		i = dprintf(fd, " %s %7ld %s\n",
				p->hash.sha1, (long int)p->hash.len,
				p->basefilename);
	}
	if (i >= 0)
		i = dprintf(fd, "X-Patch-Precedence: merged\n");
	if (i >= 0) {
		i = close(fd);
		fd = -1;
	}
	if (i < 0) {
		int e = errno;
		fprintf(stderr, "Error %d writing to '%s': %s\n",
				e, newindexfilename, strerror(e));
		if (fd >= 0)
			(void)close(fd);
		unlink(newindexfilename);
		return RET_ERRNO(e);
	}
	return RET_OK;
}

static void remove_old_diffs(const char *relfilename, const char *diffdirectory, const char *indexfilename, const struct patch *keep) {
	struct dirent *de;
	DIR *dir;
	const struct patch *p;

	if (!isdirectory(diffdirectory))
		return;

	dir = opendir(diffdirectory);
	if (dir == NULL)
		return;

	while ((de = readdir(dir)) != NULL) {
		size_t len = strlen(de->d_name);

		/* special rule for that */
		if (len == 5 && strcmp(de->d_name, "Index") == 0)
			continue;

		/* if it does not end with .gz or .gz.new, ignore */
		if (len >= 4 && memcmp(de->d_name + len - 4, ".new", 4) == 0)
			len -= 4;
		if (len < 3)
			continue;
		if (memcmp(de->d_name + len - 3, ".gz", 3) != 0)
			continue;
		len -= 3;

		/* do not mark files to be deleted we still need: */
		for (p = keep ; p != NULL ; p = p->next) {
			if (p->basefilename_len != len)
				continue;
			if (memcmp(p->basefilename, de->d_name, len) == 0)
				break;
		}
		if (p != NULL)
			continue;
		/* otherwise, tell reprepro this file is no longer needed: */
		dprintf(3, "%s.diff/%s.tobedeleted\n",
				relfilename,
				de->d_name);
	}
	closedir(dir);
	if (isregularfile(indexfilename) && keep == NULL)
		dprintf(3, "%s.diff/Index.tobedeleted\n",
				relfilename);
}

static retvalue ed_diff(const char *oldfullfilename, const char *newfullfilename, /*@out@*/struct rred_patch **rred_p) {
	const char *argv[6];
	int fd;
	retvalue r;

	argv[0] = "diff";
	argv[1] = "--ed";
	argv[2] = "--minimal";
	argv[3] = oldfullfilename;
	argv[4] = newfullfilename;
	argv[5] = NULL;

	r = execute_into_file(argv, &fd, 1);
	if (RET_WAS_ERROR(r))
		return r;

	return patch_loadfd("<temporary file>", fd, -1, rred_p);
}

static retvalue read_old_patch(const char *directory, const char *relfilename, const struct old_patch *o, /*@out@*/struct rred_patch **rred_p) {
	retvalue r;
	const char *args[4];
	char *filename;
	int fd;

	filename = mprintf("%s/%s.diff/%s.gz",
			directory, relfilename, o->basefilename);

	if (!isregularfile(filename))
		return RET_NOTHING;
	args[0] = "gunzip";
	args[1] = "-c";
	args[2] = filename;
	args[3] = NULL;

	r = execute_into_file(args, &fd, 0);
	free(filename);
	if (RET_WAS_ERROR(r))
		return r;

	return patch_loadfd("<temporary file>", fd, -1, rred_p);
}

static retvalue handle_diff(const char *directory, const char *mode, const char *relfilename, const char *fullfilename, const char *fullnewfilename, const char *diffdirectory, const char *indexfilename, const char *newindexfilename) {
	retvalue r;
	int patch_count;
	struct hash oldhash, newhash;
	char date[DATELEN + 1];
	struct patch *p, *root = NULL;
	enum {mode_OLD, mode_NEW, mode_CHANGE} m;
	struct rred_patch *new_rred_patch;
	struct modification *new_modifications;
	struct old_index_file old_index;
	struct old_patch *o;
#if defined(APT_545694_WORKAROUND) || defined(APT_545699_WORKAROUND)
	char *line;
	struct modification *newdup;
#endif

	if (strcmp(mode, "new") == 0)
		m = mode_NEW;
	else if (strcmp(mode, "old") == 0)
		m = mode_OLD;
	else if (strcmp(mode, "change") == 0)
		m = mode_CHANGE;
	else {
		usage(stderr);
		fprintf(stderr,
"Error: 4th argument to rredtool in .diff maintenance mode must be 'new', 'old' or 'change'!\n");
		return RET_ERROR;
	}

	if (m == mode_NEW) {
		/* There is no old file, nothing to do.
		 * except checking for old diff files
		 * and marking them to be deleted */
		remove_old_diffs(relfilename, diffdirectory,
				indexfilename, NULL);
		return RET_OK;
	}

	r = get_date_string(date, sizeof(date));
	if (RET_WAS_ERROR(r))
		return r;

	assert (m == mode_OLD || m == mode_CHANGE);

	/* calculate sha1 checksum of old file */
	r = gen_sha1sum(fullfilename, &oldhash);
	if (r == RET_NOTHING) {
		fprintf(stderr, "rredtool: expected file '%s' is missing!\n",
				fullfilename);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;

	if (m == mode_CHANGE) {
		/* calculate sha1 checksum of the new file */
		r = gen_sha1sum(fullnewfilename, &newhash);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"rredtool: expected file '%s' is missing!\n",
					fullnewfilename);
			r = RET_ERROR;
		}
		if (RET_WAS_ERROR(r))
			return r;

		/* if new == old, nothing to do */
		if (newhash.len == oldhash.len &&
				strcmp(newhash.sha1, oldhash.sha1) == 0) {
			m = mode_OLD;
		}
	}

	if (oldhash.len == 0 || (m == mode_CHANGE && newhash.len == 0)) {
		/* Old or new file empty. treat as mode_NEW.
		 * (checked here instead of letting later
		 * more general optimisations catch this as
		 * this garantees there are enough lines to
		 * make patches longer to work around apt bugs,
		 * and because no need to parse Index if we want to delete
		 * it anyway) */
		remove_old_diffs(relfilename, diffdirectory,
				indexfilename, NULL);
		return RET_OK;
	}

	r = read_old_index(indexfilename, &old_index);
	if (RET_WAS_ERROR(r))
		return r;

	/* ignore old Index file if it does not match the old file */
	if (old_index.hash.len != (off_t)-1 && old_index.hash.len != oldhash.len) {
		old_index_done(&old_index);
		memset(&old_index, 0, sizeof(old_index));
	}
	if (memcmp(old_index.hash.sha1, oldhash.sha1, 2*SHA1_DIGEST_SIZE) != 0) {
		old_index_done(&old_index);
		memset(&old_index, 0, sizeof(old_index));
	}

	if (m == mode_OLD) {
		/* this index file did not change.
		 * keep old or delete if not current */
		if (old_index.hash.sha1[0] != '\0') {
			for (o = old_index.first ; o != NULL ; o = o->next)
				dprintf(3, "%s.diff/%s.gz.keep\n",
						relfilename, o->basefilename);
			dprintf(3, "%s.diff/Index\n", relfilename);
		} else {
			remove_old_diffs(relfilename, diffdirectory,
					indexfilename, NULL);
		}
		old_index_done(&old_index);
		return RET_OK;
	}
	assert (m == mode_CHANGE);

	mkdir(diffdirectory, 0777);

#ifdef APT_545699_WORKAROUND
	/* create a fake diff to work around http://bugs.debian.org/545699 */
	newdup = NULL;
	r = modification_addstuff(fullnewfilename, &newdup, &line);
	if (RET_WAS_ERROR(r)) {
		modification_freelist(newdup);
		old_index_done(&old_index);
		return r;
	}
	/* save this compressed and store it's sha1sum */
	r = new_diff_file(&root, directory, relfilename, "aptbug545699+", date,
			newdup);
	modification_freelist(newdup);
	free(line);
	if (RET_WAS_ERROR(r)) {
		old_index_done(&old_index);
		return r;
	}
	root->from = newhash;
#endif

	/* create new diff calling diff --ed */
	r = ed_diff(fullfilename, fullnewfilename, &new_rred_patch);
	if (RET_WAS_ERROR(r)) {
		old_index_done(&old_index);
		patches_free(root);
		return r;
	}

	new_modifications = patch_getmodifications(new_rred_patch);
	assert (new_modifications != NULL);

#ifdef APT_545694_WORKAROUND
	newdup = modification_dup(new_modifications);
	if (RET_WAS_ERROR(r)) {
		modification_freelist(new_modifications);
		patch_free(new_rred_patch);
		old_index_done(&old_index);
		patches_free(root);
		return r;
	}
	r = modification_addstuff(fullnewfilename, &newdup, &line);
	if (RET_WAS_ERROR(r)) {
		modification_freelist(newdup);
		modification_freelist(new_modifications);
		patch_free(new_rred_patch);
		old_index_done(&old_index);
		patches_free(root);
		return r;
	}
#endif

	/* save this compressed and store it's sha1sum */
	r = new_diff_file(&root, directory, relfilename, NULL, date,
#ifdef APT_545694_WORKAROUND
			newdup);
	modification_freelist(newdup);
	free(line);
#else
			new_modifications);
#endif
	// TODO: special handling of misparsing to cope with that better?
	if (RET_WAS_ERROR(r)) {
		modification_freelist(new_modifications);
		patch_free(new_rred_patch);
		old_index_done(&old_index);
		patches_free(root);
		return r;
	}
	root->from = oldhash;

	/* if the diff is bigger than the new file,
	 * there is no point in not getting the full file.
	 * And as in all but extremely strange situations this
	 * also means all older patches will get bigger when merged,
	 * do not even bother to calculate them but remove all. */
	if (root->hash.len > newhash.len) {
		modification_freelist(new_modifications);
		patch_free(new_rred_patch);
		old_index_done(&old_index);
		patches_free(root);
		remove_old_diffs(relfilename, diffdirectory,
				indexfilename, NULL);
		return RET_OK;
	}

	patch_count = 1;
	/* merge this into the old patches */
	for (o = old_index.last ; o != NULL ; o = o->prev) {
		struct rred_patch *old_rred_patch;
		struct modification *d, *merged;

		/* ignore old and new hash, to filter out old
		 * pseudo-empty patches and to reduce the number
		 * of patches in case the file is reverted to an
		 * earlier state */
		if (memcmp(o->hash.sha1, old_index.hash.sha1,
				sizeof(old_index.hash.sha1)) == 0)
			continue;
		if (memcmp(o->hash.sha1, newhash.sha1,
				sizeof(newhash.sha1)) == 0)
			continue;
		/* limit number of patches
		 * (Index needs to be downloaded, too) */

		if (patch_count >= max_patch_count)
			continue;

		/* empty files only make problems.
		 * If you have a non-empty file with the sha1sum of an empty
		 * one: Congratulations */
		if (strcmp(o->hash.sha1,
		           "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0)
			continue;

		r = read_old_patch(directory, relfilename, o, &old_rred_patch);
		if (r == RET_NOTHING)
			continue;
		// TODO: special handling of misparsing to cope with that better?
		if (RET_WAS_ERROR(r)) {
			modification_freelist(new_modifications);
			patch_free(new_rred_patch);
			old_index_done(&old_index);
			patches_free(root);
			return r;
		}

		d = modification_dup(new_modifications);
		if (RET_WAS_ERROR(r)) {
			patch_free(old_rred_patch);
			patch_free(new_rred_patch);
			old_index_done(&old_index);
			patches_free(root);
			return r;
		}
		r = combine_patches(&merged,
				patch_getmodifications(old_rred_patch), d);
		if (RET_WAS_ERROR(r)) {
			modification_freelist(new_modifications);
			patch_free(old_rred_patch);
			patch_free(new_rred_patch);
			old_index_done(&old_index);
			patches_free(root);
			return r;
		}
		if (merged == NULL) {
			/* this should never happen as the sha1sum should
			 * already be the same, but better safe than sorry */
			patch_free(old_rred_patch);
			continue;
		}
#ifdef APT_545694_WORKAROUND
		r = modification_addstuff(fullnewfilename, &merged, &line);
		if (RET_WAS_ERROR(r)) {
			modification_freelist(merged);
			patch_free(old_rred_patch);
			modification_freelist(new_modifications);
			patch_free(new_rred_patch);
			old_index_done(&old_index);
			patches_free(root);
			return r;
		}
#endif
		r = new_diff_file(&root, directory, relfilename,
				o->nameprefix, date, merged);
		modification_freelist(merged);
#ifdef APT_545694_WORKAROUND
		free(line);
#endif
		patch_free(old_rred_patch);
		if (RET_WAS_ERROR(r)) {
			modification_freelist(new_modifications);
			patch_free(new_rred_patch);
			old_index_done(&old_index);
			patches_free(root);
			return r;
		}
		root->from = o->hash;

		/* remove patches that are bigger than the new file */
		if (root->hash.len >= newhash.len) {
			struct patch *n;

			n = root;
			root = n->next;
			n->next = NULL;
			patches_free(n);
		}
		patch_count++;
	}

	modification_freelist(new_modifications);
	patch_free(new_rred_patch);
	old_index_done(&old_index);

	assert (root != NULL);
#ifdef APT_545699_WORKAROUND
	assert (root->next != NULL);
#endif

	/* write new Index file */
	r = write_new_index(newindexfilename, &newhash, root);
	if (RET_WAS_ERROR(r)) {
		patches_free(root);
		return r;
	}

	/* tell reprepro to remove all no longer needed files */
	remove_old_diffs(relfilename, diffdirectory, indexfilename, root);

	/* tell reprepro to move those files to their final place
	 * and include the Index in the Release file */

	for (p = root ; p != NULL ; p = p->next) {
		/* the trailing . means add but do not put in Release */
		dprintf(3, "%s.diff/%s.gz.new.\n",
				relfilename, p->basefilename);
		/* no longer delete: */
		free(p->fullfilename);
		p->fullfilename = NULL;
	}
	dprintf(3, "%s.diff/Index.new\n", relfilename);
	patches_free(root);
	return RET_OK;
}

static retvalue handle_diff_dir(const char *args[4]) {
	const char *directory = args[0];
	const char *mode = args[3];
	const char *relfilename = args[2];
	const char *relnewfilename = args[1];
	char *fullfilename, *fullnewfilename;
	char *diffdirectory;
	char *indexfilename;
	char *newindexfilename;
	retvalue r;

	fullfilename = mprintf("%s/%s", directory, relfilename);
	fullnewfilename = mprintf("%s/%s", directory, relnewfilename);
	if (FAILEDTOALLOC(fullfilename) || FAILEDTOALLOC(fullnewfilename)) {
		free(fullfilename);
		free(fullnewfilename);
		return RET_ERROR_OOM;
	}
	diffdirectory = mprintf("%s.diff", fullfilename);
	indexfilename = mprintf("%s.diff/Index", fullfilename);
	newindexfilename = mprintf("%s.diff/Index.new", fullfilename);
	if (FAILEDTOALLOC(diffdirectory) || FAILEDTOALLOC(indexfilename)
			|| FAILEDTOALLOC(newindexfilename)) {
		free(diffdirectory);
		free(indexfilename);
		free(newindexfilename);
		free(fullfilename);
		free(fullnewfilename);
		return RET_ERROR_OOM;
	}
	r = handle_diff(directory, mode, relfilename,
			fullfilename, fullnewfilename, diffdirectory,
			indexfilename, newindexfilename);
	free(diffdirectory);
	free(indexfilename);
	free(newindexfilename);
	free(fullfilename);
	free(fullnewfilename);
	return r;
}

static void write_to_file(const void *data, size_t len, void *to) {
	FILE *f = to;
	fwrite(data, len, 1, f);
}

int main(int argc, const char *argv[]) {
	struct rred_patch *patches[argc];
	struct modification *m;
	retvalue r;
	bool mergemode = false;
	bool patchmode = false;
	bool repreprohook = false;
	int i, count;
	const char *sourcename;
	int debug = 0;

	while ((i = getopt_long(argc, (char**)argv, "+hVDmpR", options, NULL)) != -1) {
		switch (i) {
			case 'h':
				usage(stdout);
				return EXIT_SUCCESS;
			case 'V':
				printf(
"rred-tool from " PACKAGE_NAME " version " PACKAGE_VERSION);
				return EXIT_SUCCESS;
			case 'D':
				debug++;
				break;
			case 'm':
				mergemode = 1;
				break;
			case 'p':
				patchmode = 1;
				break;
			case 'N':
				max_patch_count = atoi(optarg);
				break;
			case 'R':
				repreprohook = 1;
				break;
			case '?':
			default:
				return EXIT_FAILURE;

		}
	}

	if (repreprohook && mergemode) {
		fprintf(stderr,
"Cannot do --reprepro-hook and --merge at the same time!\n");
		return EXIT_FAILURE;
	}
	if (repreprohook && patchmode) {
		fprintf(stderr,
"Cannot do --reprepro-hook and --patch at the same time!\n");
		return EXIT_FAILURE;
	}

	if (repreprohook || (!mergemode && !patchmode)) {
		if (optind + 4 != argc) {
			usage(stderr);
			return EXIT_FAILURE;
		}
		r = handle_diff_dir(argv + optind);
		if (r == RET_ERROR_OOM) {
			fputs("Out of memory!\n", stderr);
		}
		if (RET_WAS_ERROR(r))
			return EXIT_FAILURE;
		return EXIT_SUCCESS;
	}

	i = optind;
	if (!mergemode) {
		if (i >= argc) {
			fprintf(stderr, "Not enough arguments!\n");
			return EXIT_FAILURE;
		}
		sourcename = argv[i++];
	} else {
		SETBUTNOTUSED( sourcename = NULL; )
	}
	if (mergemode && patchmode) {
		fprintf(stderr,
"Cannot do --merge and --patch at the same time!\n");
		return EXIT_FAILURE;
	}

	count = 0;
	while (i < argc) {
		r = patch_load(argv[i], -1, &patches[count]);
		if (RET_IS_OK(r))
			count++;
		if (RET_WAS_ERROR(r)) {
			if (r == RET_ERROR_OOM)
				fputs("Out of memory!\n", stderr);
			else
				fputs("Aborting...\n", stderr);
			return EXIT_FAILURE;
		}
		i++;
	}
	if (count <= 0) {
		fprintf(stderr, "Not enough patches for operation...\n");
		return EXIT_FAILURE;
	}
	m = patch_getmodifications(patches[0]);
	for (i = 1; i < count ; i++) {
		struct modification *a = patch_getmodifications(patches[i]);
		if (debug) {
			fputs("--------RESULT SO FAR--------\n", stderr);
			modification_printaspatch(stderr, m, write_to_file);
			fputs("--------TO BE MERGED WITH-----\n", stderr);
			modification_printaspatch(stderr, a, write_to_file);
			fputs("-------------END--------------\n", stderr);
		}
		r = combine_patches(&m, m, a);
		if (RET_WAS_ERROR(r)) {
			for (i = 0 ; i < count ; i++) {
				patch_free(patches[i]);
			}
			if (r == RET_ERROR_OOM)
				fputs("Out of memory!\n", stderr);
			else
				fputs("Aborting...\n", stderr);
			return EXIT_FAILURE;
		}
	}
	r = RET_OK;
	if (mergemode) {
		modification_printaspatch(stdout, m, write_to_file);
	} else {
		r = patch_file(stdout, sourcename, m);
	}
	if (ferror(stdout)) {
		fputs("Error writing to stdout!\n", stderr);
		r = RET_ERROR;
	}
	modification_freelist(m);
	for (i = 0 ; i < count ; i++)
		patch_free(patches[i]);
	if (r == RET_ERROR_OOM)
		fputs("Out of memory!\n", stderr);
	if (RET_WAS_ERROR(r))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
