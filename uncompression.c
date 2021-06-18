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

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <zlib.h>
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#endif
#ifdef HAVE_LIBLZMA
#include <lzma.h>
#endif

#include "globals.h"
#include "error.h"
#include "mprintf.h"
#include "filecntl.h"
#include "uncompression.h"

const char * const uncompression_suffix[c_COUNT] = {
	"", ".gz", ".bz2", ".lzma", ".xz", ".lz", ".zst"};

/* So help messages can hint what option to try */
const char * const uncompression_option[c_COUNT] = {
	NULL, NULL, "--bunzip2", "--unlzma", "--unxz", "--lunzip", "--unzstd" };
/* how those are called in the config file */
const char * const uncompression_config[c_COUNT] = {
	".", ".gz", ".bz2", ".lzma", ".xz", ".lz", ".zst" };


/*@null@*/ char *extern_uncompressors[c_COUNT] = {
	NULL, NULL, NULL, NULL, NULL, NULL};

/*@null@*/ static struct uncompress_task {
	struct uncompress_task *next;
	enum compression compression;
	char *compressedfilename;
	char *uncompressedfilename;
	/* when != NULL, call when finished */
	/*@null@*/finishaction *callback;
	/*@null@*/void *privdata;
	/* if already started, the pid > 0 */
	pid_t pid;
} *tasks = NULL;

static void uncompress_task_free(/*@only@*/struct uncompress_task *t) {
	free(t->compressedfilename);
	free(t->uncompressedfilename);
	free(t);
}

static retvalue startchild(enum compression c, int stdinfd, int stdoutfd, /*@out@*/pid_t *pid_p) {
	int e, i;
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		e = errno;
		fprintf(stderr, "Error %d forking: %s\n", e, strerror(e));
		(void)close(stdinfd);
		(void)close(stdoutfd);
		return RET_ERRNO(e);
	}
	if (pid == 0) {
		/* setup child */
		i = dup2(stdoutfd, 1);
		if (i < 0) {
			e = errno;
			fprintf(stderr, "Error %d in dup(%d, 1): %s\n",
					e, stdoutfd, strerror(e));
			raise(SIGUSR2);
		}
		i = dup2(stdinfd, 0);
		if (i < 0) {
			e = errno;
			fprintf(stderr, "Error %d in dup(%d, 0): %s\n",
					e, stdinfd, strerror(e));
			raise(SIGUSR2);
		}
		closefrom(3);
		execlp(extern_uncompressors[c], extern_uncompressors[c],
				ENDOFARGUMENTS);
		e = errno;
		fprintf(stderr, "Error %d starting '%s': %s\n",
				e, extern_uncompressors[c], strerror(e));
		raise(SIGUSR2);
		exit(EXIT_FAILURE);
	}
	(void)close(stdinfd);
	(void)close(stdoutfd);
	*pid_p = pid;
	return RET_OK;
}

static retvalue startpipeoutchild(enum compression c, int fd, /*@out@*/int *pipefd, /*@out@*/pid_t *pid_p) {
	int i, e, filedes[2];
	retvalue r;

	i = pipe(filedes);
	if (i < 0) {
		e = errno;
		fprintf(stderr, "Error %d creating pipe: %s\n", e, strerror(e));
		(void)close(fd);
		return RET_ERRNO(e);
	}
	markcloseonexec(filedes[0]);
	r = startchild(c, fd, filedes[1], pid_p);
	if (RET_WAS_ERROR(r))
		/* fd and filedes[1] are closed by startchild on error */
		(void)close(filedes[0]);
	else
		*pipefd = filedes[0];
	return r;
}

static retvalue startpipeinoutchild(enum compression c, /*@out@*/int *infd, /*@out@*/int *outfd, /*@out@*/pid_t *pid_p) {
	int i, e, infiledes[2];
	retvalue r;

	i = pipe(infiledes);
	if (i < 0) {
		e = errno;
		fprintf(stderr, "Error %d creating pipe: %s\n", e, strerror(e));
		return RET_ERRNO(e);
	}
	markcloseonexec(infiledes[1]);
	r = startpipeoutchild(c, infiledes[0], outfd, pid_p);
	if (RET_WAS_ERROR(r))
		/* infiledes[0] is closed by startpipeoutchild on error */
		(void)close(infiledes[1]);
	else
		*infd = infiledes[1];
	return r;
}

static retvalue uncompress_start_queued(void) {
	struct uncompress_task *t;
	int running_count = 0;
	int e, stdinfd, stdoutfd;

	for (t = tasks ; t != NULL ; t = t->next) {
		if (t->pid > 0)
			running_count++;
	}
	// TODO: make the maximum number configurable,
	// until that 1 is the best guess...
	if (running_count >= 1)
		return RET_OK;
	t = tasks;
	while (t != NULL && t->pid > 0)
		t = t->next;
	if (t == NULL)
		/* nothing to do... */
		return RET_NOTHING;
	if (verbose > 1) {
		fprintf(stderr, "Uncompress '%s' into '%s' using '%s'...\n",
				t->compressedfilename,
				t->uncompressedfilename,
				extern_uncompressors[t->compression]);
	}
	stdinfd = open(t->compressedfilename, O_RDONLY|O_NOCTTY);
	if (stdinfd < 0) {
		e = errno;
		fprintf(stderr, "Error %d opening %s: %s\n",
				e, t->compressedfilename,
				strerror(e));
		// TODO: call callback
		return RET_ERRNO(e);
	}
	stdoutfd = open(t->uncompressedfilename,
			O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW, 0666);
	if (stdoutfd < 0) {
		close(stdinfd);
		e = errno;
		fprintf(stderr, "Error %d creating %s: %s\n",
				e, t->uncompressedfilename,
				strerror(e));
		// TODO: call callback
		return RET_ERRNO(e);
	}
	return startchild(t->compression, stdinfd, stdoutfd, &t->pid);
}

static inline retvalue builtin_uncompress(const char *compressed, const char *destination, enum compression compression);

/* we got an pid, check if it is a uncompressor we care for */
retvalue uncompress_checkpid(pid_t pid, int status) {
	struct uncompress_task *t, **t_p;
	retvalue r, r2;
	bool error = false;

	if (pid <= 0)
		return RET_NOTHING;
	t_p = &tasks;
	while ((t = (*t_p)) != NULL && t->pid != pid)
		t_p = &t->next;
	if (t == NULL) {
		/* not one we started */
		return RET_NOTHING;
	}
	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			fprintf(stderr,
"'%s' < %s > %s exited with errorcode %d!\n",
					extern_uncompressors[t->compression],
					t->compressedfilename,
					t->uncompressedfilename,
					(int)(WEXITSTATUS(status)));
			error = true;
		}
	} else if (WIFSIGNALED(status)) {
		if (WTERMSIG(status) != SIGUSR2)
			fprintf(stderr, "'%s' < %s > %s killed by signal %d!\n",
					extern_uncompressors[t->compression],
					t->compressedfilename,
					t->uncompressedfilename,
					(int)(WTERMSIG(status)));
		error = true;
	} else {
		fprintf(stderr, "'%s' < %s > %s terminated abnormally!\n",
				extern_uncompressors[t->compression],
					t->compressedfilename,
					t->uncompressedfilename);
		error = true;
	}
	if (error) {
		/* no need to leave partial stuff around */
		(void)unlink(t->uncompressedfilename);
	}
	if (!error && verbose > 10)
		printf("'%s' < %s > %s finished successfully!\n",
				extern_uncompressors[t->compression],
					t->compressedfilename,
					t->uncompressedfilename);
	if (error && uncompression_builtin(t->compression)) {
		/* try builtin method instead */
		r = builtin_uncompress(t->compressedfilename,
				t->uncompressedfilename, t->compression);
		if (RET_WAS_ERROR(r)) {
			(void)unlink(t->uncompressedfilename);
		} else if (RET_IS_OK(r)) {
			error = false;
		}
	}
	/* call the notification, if asked for */
	if (t->callback != NULL) {
		r = t->callback(t->privdata, t->compressedfilename, error);
		if (r == RET_NOTHING)
			r = RET_OK;
	} else if (error)
		r = RET_ERROR;
	else
		r = RET_OK;
	/* take out of the chain and free */
	*t_p = t->next;
	uncompress_task_free(t);
	r2 = uncompress_start_queued();
	RET_ENDUPDATE(r, r2);
	return r;
}

bool uncompress_running(void) {
	uncompress_start_queued();
	return tasks != NULL;
}

/* check if a program is available. This is needed because things like execlp
 * are to late (we want to know if downloading a Packages.bz2 does make sense
 * when compiled without libbz2 before actually calling the uncompressor) */

static void search_binary(/*@null@*/const char *setting, const char *default_program, /*@out@*/char **program_p) {
	char *program;
	const char *path, *colon;

	/* not set or empty means default */
	if (setting == NULL || setting[0] == '\0')
		setting = default_program;
	/* all-caps NONE means I do not want any... */
	if (strcmp(setting, "NONE") == 0)
		return;
	/* look for the file, look in $PATH if not qualified,
	 * only check existence, if someone it putting files not executable
	 * by us there it is their fault (as being executable by us is hard
	 * to check) */
	if (strchr(setting, '/') != NULL) {
		if (!isregularfile(setting))
			return;
		if (access(setting, X_OK) != 0)
			return;
		program = strdup(setting);
	} else {
		path = getenv("PATH");
		if (path == NULL)
			return;
		program = NULL;
		while (program == NULL && path[0] != '\0') {
			if (path[0] == ':') {
				path++;
				continue;
			}
			colon = strchr(path, ':');
			if (colon == NULL)
				colon = path + strlen(path);
			assert (colon > path);
			program = mprintf("%.*s/%s", (int)(colon - path), path,
					setting);
			if (program == NULL)
				return;
			if (!isregularfile(program) ||
					access(program, X_OK) != 0) {
				free(program);
				program = NULL;
			}
			if (*colon == ':')
				path = colon + 1;
			else
				path = colon;
		}
	}
	if (program == NULL)
		return;

	*program_p = program;
}

/* check for existence of external programs */
void uncompressions_check(const char *gunzip, const char *bunzip2, const char *unlzma, const char *unxz, const char *lunzip, const char *unzstd) {
	search_binary(gunzip,  "gunzip",  &extern_uncompressors[c_gzip]);
	search_binary(bunzip2, "bunzip2", &extern_uncompressors[c_bzip2]);
	search_binary(unlzma,  "unlzma",  &extern_uncompressors[c_lzma]);
	search_binary(unxz,    "unxz",    &extern_uncompressors[c_xz]);
	search_binary(lunzip,  "lunzip",  &extern_uncompressors[c_lunzip]);
	search_binary(unzstd,  "unzstd",  &extern_uncompressors[c_zstd]);
}

static inline retvalue builtin_uncompress(const char *compressed, const char *destination, enum compression compression) {
	struct compressedfile *f;
	char buffer[4096];
	int bytes_read, bytes_written, written;
	int destfd;
	int e;
	retvalue r;

	r = uncompress_open(&f, compressed, compression);
	if (!RET_IS_OK(r))
		return r;
	destfd = open(destination, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY, 0666);
	if (destfd < 0) {
		e = errno;
		fprintf(stderr, "Error %d creating '%s': %s\n",
				e, destination, strerror(e));
		uncompress_abort(f);
		return RET_ERRNO(e);
	}
	do {
		bytes_read = uncompress_read(f, buffer, 4096);
		if (bytes_read <= 0)
			break;

		bytes_written = 0;
		while (bytes_written < bytes_read) {
			written = write(destfd, buffer + bytes_written,
					bytes_read - bytes_written);
			if (written < 0) {
				e = errno;
				fprintf(stderr,
"Error %d writing to '%s': %s\n",
					e, destination, strerror(e));
				close(destfd);
				uncompress_abort(f);
				return RET_ERRNO(e);
			}
			bytes_written += written;
		}
	} while (true);
	r = uncompress_close(f);
	if (RET_WAS_ERROR(r)) {
		(void)close(destfd);
		return r;
	}
	if (close(destfd) != 0) {
		e = errno;
		fprintf(stderr, "Error %d writing to '%s': %s!\n",
				e, destination, strerror(e));
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue uncompress_queue_external(enum compression compression, const char *compressed, const char *uncompressed, /*@null@*/finishaction *action, /*@null@*/void *privdata) {
	struct uncompress_task *t, **t_p;
	retvalue r;

	t_p = &tasks;
	while ((t = (*t_p)) != NULL)
		t_p = &t->next;

	t = zNEW(struct uncompress_task);
	if (FAILEDTOALLOC(t))
		return RET_ERROR_OOM;

	t->compressedfilename = strdup(compressed);
	t->uncompressedfilename = strdup(uncompressed);
	if (FAILEDTOALLOC(t->compressedfilename) ||
	    FAILEDTOALLOC(t->uncompressedfilename)) {
		uncompress_task_free(t);
		return RET_ERROR_OOM;
	}
	t->compression = compression;
	t->callback = action;
	t->privdata = privdata;
	*t_p = t;
	r = uncompress_start_queued();
	if (r == RET_NOTHING)
		r = RET_ERROR_INTERNAL;
	return r;
}

retvalue uncompress_queue_file(const char *compressed, const char *destination, enum compression compression, finishaction *action, void *privdata) {
	retvalue r;

	(void)unlink(destination);
	if (extern_uncompressors[compression] != NULL) {
		r = uncompress_queue_external(compression, compressed,
				destination, action, privdata);
		if (r != RET_NOTHING) {
			return r;
		}
		if (!uncompression_builtin(compression))
			return RET_ERROR;
	}
	if (verbose > 1) {
		fprintf(stderr, "Uncompress '%s' into '%s'...\n",
				compressed, destination);
	}
	assert (uncompression_builtin(compression));
	r = builtin_uncompress(compressed, destination, compression);
	if (RET_WAS_ERROR(r)) {
		(void)unlink(destination);
		return r;
	}
	return action(privdata, compressed, false);
}

retvalue uncompress_file(const char *compressed, const char *destination, enum compression compression) {
	retvalue r;

	/* not allowed within a aptmethod session */
	assert (tasks == NULL);

	(void)unlink(destination);
	if (uncompression_builtin(compression)) {
		if (verbose > 1) {
			fprintf(stderr, "Uncompress '%s' into '%s'...\n",
					compressed, destination);
		}
		r = builtin_uncompress(compressed, destination, compression);
	} else if (extern_uncompressors[compression] != NULL) {
		r = uncompress_queue_external(compression,
				compressed, destination, NULL, NULL);
		if (r == RET_NOTHING)
			r = RET_ERROR;
		if (RET_IS_OK(r)) {
			/* wait for the child to finish... */
			assert (tasks != NULL && tasks->next == NULL);

			do {
				int status;
				pid_t pid;

				pid = wait(&status);

				if (pid < 0) {
					int e = errno;

					if (interrupted()) {
						r = RET_ERROR_INTERRUPTED;
						break;
					}
					if (e == EINTR)
						continue;
					fprintf(stderr,
"Error %d waiting for uncompression child: %s\n",
						e, strerror(e));
					r = RET_ERRNO(e);
				} else
					r = uncompress_checkpid(pid, status);
			} while (r == RET_NOTHING);
		}
	} else {
		assert ("Impossible uncompress error" == NULL);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		(void)unlink(destination);
		return r;
	}
	return RET_OK;
}

struct compressedfile {
	char *filename;
	enum compression compression;
	bool external;
	bool closefd;
	int error;
	pid_t pid;
	int fd, infd, pipeinfd;
	off_t len;
	union {
		/* used with an external decompressor if the input fd cannot
		 * be used as that programs stdin directly: */
		struct intermediate_buffer {
			char *buffer;
			int ofs;
			int ready;
		} intermediate;
		/* used if an internal decompression != c_none is used: */
		struct uncompression {
			unsigned char *buffer;
			unsigned int available;
			union {
				z_stream gz;
#ifdef HAVE_LIBBZ2
				bz_stream bz2;
#endif
#ifdef HAVE_LIBLZMA
				lzma_stream lzma;
#endif
			};
			enum uncompression_error {
				ue_NO_ERROR = 0,
				ue_TRAILING_GARBAGE,
				ue_WRONG_LENGTH,
				ue_UNCOMPRESSION_ERROR,
			} error;
			/* compression stream ended */
			bool hadeos;
		} uncompress;
	};
};

/* This function is called to refill the internal buffer in uncompress.buffer
 * with data initially or one everything of the previous run was consumed.
 * It will set uncompress.available to a value >0, unless there is a EOF
 * condition, then it can also be set to 0.
 */
#define UNCOMPRESSION_BUFSIZE 16*1024
static retvalue uncompression_read_internal_buffer(struct compressedfile *f) {
	size_t len;
	ssize_t r;
	assert (f->uncompress.available == 0);

	if (f->len == 0) {
		f->uncompress.available = 0;
		return RET_OK;
	}

	if (f->uncompress.buffer == NULL) {
		f->uncompress.buffer = malloc(UNCOMPRESSION_BUFSIZE);
		if (FAILEDTOALLOC(f->uncompress.buffer)) {
			f->error = ENOMEM;
			return RET_ERROR_OOM;
		}
	}

	len = UNCOMPRESSION_BUFSIZE;
	if (f->len >= 0 && len > (size_t)f->len)
		len = f->len;

	if (len == 0)
		return RET_OK;

	do {
		if (interrupted()) {
			f->error = EINTR;
			return RET_ERROR_INTERRUPTED;
		}
		r = read(f->fd, f->uncompress.buffer, len);
	} while (r < 0 && errno == EINTR);
	if (r < 0) {
		f->error = errno;
		return RET_ERRNO(errno);
	}
	assert ((size_t)r <= len);
	if (f->len >= 0) {
		assert (r <= f->len);
		f->len -= r;
	} else {
		if (r == 0) {
			/* remember EOF
			 * (so it can be checked for to detect
			 * checksum circumventing trailing garbage) */
			f->len = 0;
		}
	}
	f->uncompress.available = r;
	return RET_OK;
}

static inline retvalue start_gz(struct compressedfile *f, int *errno_p, const char **msg_p) {
	int ret;

	memset(&f->uncompress.gz, 0, sizeof(f->uncompress.gz));
	f->uncompress.gz.zalloc = Z_NULL; /* use default */
	f->uncompress.gz.zfree = Z_NULL; /* use default */
	f->uncompress.gz.next_in = f->uncompress.buffer;
	f->uncompress.gz.avail_in = f->uncompress.available;
	/* 32 means accept zlib and gz header
	 * 15 means accept any windowSize */
	ret = inflateInit2(&f->uncompress.gz, 32 + 15);
	if (ret != Z_OK) {
		if (ret == Z_MEM_ERROR) {
			*errno_p = ENOMEM;
			*msg_p = "Out of Memory";
			return RET_ERROR_OOM;
		}
		*errno_p = -1;
		/* f->uncompress.gz.msg will be free'd soon */
		fprintf(stderr, "zlib error %d: %s", ret, f->uncompress.gz.msg);
		*msg_p = "Error starting internal gz uncompression using zlib";
		return RET_ERROR;
	}
	return RET_OK;
}

#ifdef HAVE_LIBBZ2
static inline retvalue start_bz2(struct compressedfile *f, int *errno_p, const char **msg_p) {
	int ret;

	memset(&f->uncompress.bz2, 0, sizeof(f->uncompress.bz2));

	/* not used by bzDecompressInit, but not set before next call: */
	f->uncompress.bz2.next_in = (char*)f->uncompress.buffer;
	f->uncompress.bz2.avail_in = f->uncompress.available;

	ret = BZ2_bzDecompressInit(&f->uncompress.bz2, 0, 0);
	if (ret != BZ_OK) {
		if (ret == BZ_MEM_ERROR) {
			*errno_p = ENOMEM;
			*msg_p = "Out of Memory";
			return RET_ERROR_OOM;
		}
		*errno_p = -EINVAL;
		*msg_p = "libbz2 not working";
		return RET_ERROR;
	}
	return RET_OK;
}
#endif

#ifdef HAVE_LIBLZMA
static inline retvalue start_lzma(struct compressedfile *f, int *errno_p, const char **msg_p) {
	int ret;
	/* as the API requests: */
	lzma_stream tmpstream = LZMA_STREAM_INIT;

	memset(&f->uncompress.lzma, 0, sizeof(f->uncompress.lzma));
	f->uncompress.lzma = tmpstream;

	/* not used here, but needed by uncompression_read_* logic */
	f->uncompress.lzma.next_in = f->uncompress.buffer;
	f->uncompress.lzma.avail_in = f->uncompress.available;

	ret = lzma_alone_decoder(&f->uncompress.lzma, UINT64_MAX);
	if (ret != LZMA_OK) {
		if (ret == LZMA_MEM_ERROR) {
			*errno_p = ENOMEM;
			*msg_p = "Out of Memory";
			return RET_ERROR_OOM;
		}
		*errno_p = -EINVAL;
		*msg_p = "liblzma not working";
		return RET_ERROR;
	}
	return RET_OK;
}

static inline retvalue start_xz(struct compressedfile *f, int *errno_p, const char **msg_p) {
	int ret;
	/* as the API requests: */
	lzma_stream tmpstream = LZMA_STREAM_INIT;

	memset(&f->uncompress.lzma, 0, sizeof(f->uncompress.lzma));
	f->uncompress.lzma = tmpstream;

	/* not used here, but needed by uncompression_read_* logic */
	f->uncompress.lzma.next_in = f->uncompress.buffer;
	f->uncompress.lzma.avail_in = f->uncompress.available;

	ret = lzma_stream_decoder(&f->uncompress.lzma, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		if (ret == LZMA_MEM_ERROR) {
			*errno_p = ENOMEM;
			*msg_p = "Out of Memory";
			return RET_ERROR_OOM;
		}
		*errno_p = -EINVAL;
		*msg_p = "liblzma not working";
		return RET_ERROR;
	}
	return RET_OK;
}
#endif

static retvalue start_builtin(struct compressedfile *f, int *errno_p, const char **msg_p) {
	retvalue r;

	assert (f->compression != c_none);
	assert (uncompression_builtin(f->compression));

	r = uncompression_read_internal_buffer(f);
	if (RET_WAS_ERROR(r)) {
		free(f->uncompress.buffer);
		f->uncompress.buffer = NULL;
		*errno_p = f->error;
		*msg_p = strerror(f->error);
		return r;
	}
	if (f->uncompress.available == 0) {
		*errno_p = -EINVAL;
		*msg_p = "File supposed to be compressed file is empty instead";
		return RET_ERROR;
	}

	switch (f->compression) {
		case c_gzip:
			return start_gz(f, errno_p, msg_p);
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			return start_bz2(f, errno_p, msg_p);
#endif
#ifdef HAVE_LIBLZMA
		case c_lzma:
			return start_lzma(f, errno_p, msg_p);
		case c_xz:
			return start_xz(f, errno_p, msg_p);
#endif
		default:
			assert (false);
			return RET_ERROR_INTERNAL;
	}
	/* not reached */
}

retvalue uncompress_open(/*@out@*/struct compressedfile **file_p, const char *filename, enum compression compression) {
	struct compressedfile *f;
	int fd, e;
	retvalue r;
	const char *msg;

	f = zNEW(struct compressedfile);
	if (FAILEDTOALLOC(f))
		return RET_ERROR_OOM;
	f->filename = strdup(filename);
	if (FAILEDTOALLOC(f->filename)) {
		free(f);
		return RET_ERROR_OOM;
	}
	f->compression = compression;
	f->fd = -1;
	f->infd = -1;
	f->pipeinfd = -1;
	f->len = -1;
	f->external = false;
	f->closefd = true;

	if (compression == c_none || uncompression_builtin(compression)) {
		f->fd = open(filename, O_RDONLY|O_NOCTTY);
		if (f->fd < 0) {
			e = errno;
			free(f->filename);
			free(f);
			// if (e == || e ==)
			//	return RET_NOTHING;
			fprintf(stderr, "Error %d opening '%s': %s!\n",
					e, filename, strerror(e));
			return RET_ERRNO(e);
		}
		if (f->compression != c_none) {
			r = start_builtin(f, &e, &msg);
			if (RET_WAS_ERROR(r)) {
				(void)close(f->fd);
				if (e != -EINVAL && e != 0)
					fprintf(stderr,
"Error %d stating unpacking '%s': %s\n",
						e, f->filename, msg);
				else
					fprintf(stderr,
"Error starting unpacking '%s': %s\n",
						f->filename, msg);
				free(f->filename);
				free(f);
				return r;
			}
		}
	} else {
		assert (extern_uncompressors[compression] != NULL);
		/* call external helper instead */
		fd = open(f->filename, O_RDONLY|O_NOCTTY);
		if (fd < 0) {
			e = errno;
			fprintf(stderr, "Error %d opening '%s': %s\n", e,
					f->filename, strerror(e));
			free(f->filename);
			free(f);
			return RET_ERRNO(e);
		}
		/* startpipeoutchild closes fd on error: */
		r = startpipeoutchild(compression, fd, &f->fd, &f->pid);
		if (RET_WAS_ERROR(r)) {
			free(f->filename);
			free(f);
			return r;
		}
		assert (f->pid > 0);
		f->external = true;
	}
	*file_p = f;
	return RET_OK;
}

static int intermediate_size = 0;

retvalue uncompress_fdopen(struct compressedfile **file_p, int fd, off_t len, enum compression compression, int *errno_p, const char **msg_p) {
	struct compressedfile *f;
	retvalue r;

	f = zNEW(struct compressedfile);
	if (FAILEDTOALLOC(f)) {
		*errno_p = ENOMEM;
		*msg_p = "Out of memory";
		return RET_ERROR_OOM;
	}
	f->filename = NULL;
	f->compression = compression;
	f->infd = fd;
	f->fd = -1;
	f->pipeinfd = -1;
	f->len = len;
	f->external = false;
	f->closefd = false;

	if (compression == c_none) {
		f->fd = fd;
		f->infd = -1;
	} else if (uncompression_builtin(compression)) {
		f->fd = fd;
		f->infd = -1;
		r = start_builtin(f, errno_p, msg_p);
		if (RET_WAS_ERROR(r)) {
			free(f);
			return r;
		}
	} else {
		assert (extern_uncompressors[compression] != NULL);

		f->external = true;
		if (intermediate_size == 0) {
			/* pipes are guaranteed to swallow a full
			 * page without blocking if poll
			 * tells you can write */
			long l = sysconf(_SC_PAGESIZE);
			if (l <= 0)
				intermediate_size = 512;
			else if (l > 4096)
				intermediate_size = 4096;
			else
				intermediate_size = l;
		}
		f->intermediate.buffer = malloc(intermediate_size);
		f->intermediate.ready = 0;
		f->intermediate.ofs = 0;
		if (FAILEDTOALLOC(f->intermediate.buffer)) {
			*errno_p = ENOMEM;
			*msg_p = "Out of memory";
			free(f);
			return RET_ERROR_OOM;
		}
		r = startpipeinoutchild(f->compression,
				&f->pipeinfd, &f->fd, &f->pid);
		if (RET_WAS_ERROR(r)) {
			*errno_p = -EINVAL;
			*msg_p = "Error starting external uncompressor";
			free(f->intermediate.buffer);
			free(f);
			return r;
		}
	}
	*file_p = f;
	return RET_OK;
}

static inline int pipebackforth(struct compressedfile *file, void *buffer, int size) {
	/* we have to make sure we only read when things are available and only
	 * write when there is still space in the pipe, otherwise we can end up
	 * in a because we are waiting for the output of a program that cannot
	 * generate output because it needs more input from us first or because
	 * we wait for a program to accept input that waits for us to consume
	 * the output... */
	struct pollfd p[2];
	ssize_t written;
	int i;

	assert (file->external);

	do {

		p[0].fd = file->pipeinfd;
		p[0].events = POLLOUT;
		p[1].fd = file->fd;
		p[1].events = POLLIN;

		/* wait till there is something to do */
		i = poll(p, 2, -1);
		if (i < 0) {
			if (errno == EINTR)
				continue;
			file->error = errno;
			return -1;
		}
		if ((p[0].revents & POLLERR) != 0) {
			file->error = EIO;
			return -1;
		}
		if ((p[0].revents & POLLHUP) != 0) {
			/* not being able to send when we have something
			 * is an error */
			if (file->len > 0 || file->intermediate.ready > 0) {
				file->error = EIO;
				return -1;
			}
			(void)close(file->pipeinfd);
			file->pipeinfd = -1;
			/* wait for the rest */
			return read(file->fd, buffer, size);

		}
		if ((p[0].revents & POLLOUT) != 0) {
			struct intermediate_buffer *im = &file->intermediate;

			if (im->ready < 0)
				return -1;

			if (im->ready == 0) {
				// TODO: check if splice is safe or will create
				// dead-locks...
				int isize = intermediate_size;
				im->ofs = 0;

				if (file->len >= 0 && isize > file->len)
					isize = file->len;
				if (isize == 0)
					im->ready = 0;
				else
					im->ready = read(file->infd,
							im->buffer + im->ofs,
							isize);
				if (im->ready < 0) {
					file->error = errno;
					return -1;
				}
				if (im->ready == 0) {
					(void)close(file->pipeinfd);
					file->pipeinfd = -1;
					/* wait for the rest */
					return read(file->fd, buffer, size);
				}
				file->len -= im->ready;
			}
			written = write(file->pipeinfd, im->buffer + im->ofs,
					im->ready);
			if (written < 0) {
				file->error = errno;
				return -1;
			}
			im->ofs += written;
			im->ready -= written;
		}

		if ((p[1].revents & POLLIN) != 0)
			return read(file->fd, buffer, size);
	} while (true);
}

static inline int restart_gz_if_needed(struct compressedfile *f) {
	retvalue r;
	int ret;

	/* first mark end of stream, will be reset if restarted */
	f->uncompress.hadeos = true;

	if (f->uncompress.gz.avail_in == 0 && f->len != 0) {
		/* Input buffer consumed and (possibly) more data, so
		 * read more data to check:  */
		f->uncompress.available = 0;
		r = uncompression_read_internal_buffer(f);
		if (RET_WAS_ERROR(r))
			return false;
		f->uncompress.gz.next_in = f->uncompress.buffer;
		f->uncompress.gz.avail_in = f->uncompress.available;
		if (f->uncompress.available == 0 && f->len > 0) {
			/* stream ends, file ends, but we are
			 * still expecting data? */
			f->uncompress.error = ue_WRONG_LENGTH;
			return false;
		}
		assert (f->uncompress.gz.avail_in > 0 || f->len == 0);
	}
	if (f->uncompress.gz.avail_in > 0 &&
			f->uncompress.gz.next_in[0] == 0x1F) {
		/* might be concatenated files, so try to restart */
		ret = inflateEnd(&f->uncompress.gz);
		if (ret != Z_OK) {
			f->uncompress.error = ue_UNCOMPRESSION_ERROR;
			return false;
		}

		unsigned int avail_in = f->uncompress.gz.avail_in;
		unsigned char *next_in =  f->uncompress.gz.next_in;
		memset(&f->uncompress.gz, 0, sizeof(f->uncompress.gz));
		f->uncompress.gz.zalloc = Z_NULL; /* use default */
		f->uncompress.gz.zfree = Z_NULL; /* use default */
		f->uncompress.gz.next_in = next_in;
		f->uncompress.gz.avail_in = avail_in;
		/* 32 means accept zlib and gz header
		 * 15 means accept any windowSize */
		ret = inflateInit2(&f->uncompress.gz, 32 + 15);
		if (ret != BZ_OK) {
			if (ret == BZ_MEM_ERROR) {
				f->error = ENOMEM;
				return false;
			}
			f->uncompress.error = ue_UNCOMPRESSION_ERROR;
			return false;
		}
		if (ret != Z_OK) {
			if (ret == Z_MEM_ERROR) {
				f->error = ENOMEM;
				return false;
			}
			f->uncompress.error = ue_TRAILING_GARBAGE;
			return false;
		}
		f->uncompress.hadeos = false;
		/* successful restarted */
		return true;
	} else {
		/* mark End Of Stream, so bzDecompress is not called again */
		f->uncompress.hadeos = true;
		if (f->uncompress.gz.avail_in > 0) {
			/* trailing garbage */
			f->uncompress.error = ue_TRAILING_GARBAGE;
			return false;
		} else
			/* normal end of stream, no error and
			 * no restart necessary: */
			return true;
	}
}

static inline int read_gz(struct compressedfile *f, void *buffer, int size) {
	int ret;
	int flush = Z_SYNC_FLUSH;
	retvalue r;

	assert (f->compression == c_gzip);
	assert (size >= 0);

	if (size == 0)
		return 0;

	f->uncompress.gz.next_out = buffer;
	f->uncompress.gz.avail_out = size;
	do {

		if (f->uncompress.gz.avail_in == 0) {
			f->uncompress.available = 0;
			r = uncompression_read_internal_buffer(f);
			if (RET_WAS_ERROR(r)) {
				f->error = errno;
				return -1;
			}
			f->uncompress.gz.next_in = f->uncompress.buffer;
			f->uncompress.gz.avail_in = f->uncompress.available;
		}

		/* as long as there is new data, never do Z_FINISH */
		if (f->uncompress.gz.avail_in != 0)
			flush = Z_SYNC_FLUSH;

		ret = inflate(&f->uncompress.gz, flush);

		if (ret == Z_STREAM_END) {
			size_t gotdata = size - f->uncompress.gz.avail_out;

			f->uncompress.gz.next_out = NULL;
			f->uncompress.gz.avail_out = 0;

			if (!restart_gz_if_needed(f))
				return -1;
			if (gotdata > 0 || f->uncompress.hadeos)
				return gotdata;

			/* read the restarted stream for data */
			ret = Z_OK;
			flush = Z_SYNC_FLUSH;
			f->uncompress.gz.next_out = buffer;
			f->uncompress.gz.avail_out = size;
		} else {
			/* use Z_FINISH on second try, unless there is new data */
			flush = Z_FINISH;
		}

		/* repeat if no output was produced,
		 * assuming zlib will consume all input otherwise,
		 * as the documentation says: */
	} while (ret == Z_OK && f->uncompress.gz.avail_out == (size_t)size);
	if (ret == Z_OK ||
  	    (ret == Z_BUF_ERROR && f->uncompress.gz.avail_out != (size_t)size)) {
		return size - f->uncompress.gz.avail_out;
	} else if (ret == Z_MEM_ERROR) {
		fputs("Out of memory!", stderr);
		f->error = ENOMEM;
		return -1;
	} else {
		// TODO: more information about what is decompressed?
		fprintf(stderr, "Error decompressing gz data: %s %d\n",
				f->uncompress.gz.msg, ret);
		f->uncompress.error = ue_UNCOMPRESSION_ERROR;
		return -1;
	}
	/* not reached */
}

#ifdef HAVE_LIBBZ2
static inline int restart_bz2_if_needed(struct compressedfile *f) {
	retvalue r;
	int ret;

	/* first mark end of stream, will be reset if restarted */
	f->uncompress.hadeos = true;

	if (f->uncompress.bz2.avail_in == 0 && f->len != 0) {
		/* Input buffer consumed and (possibly) more data, so
		 * read more data to check:  */
		f->uncompress.available = 0;
		r = uncompression_read_internal_buffer(f);
		if (RET_WAS_ERROR(r))
			return false;
		f->uncompress.bz2.next_in = (char*)f->uncompress.buffer;
		f->uncompress.bz2.avail_in = f->uncompress.available;
		if (f->uncompress.available == 0 && f->len > 0) {
			/* stream ends, file ends, but we are
			 * still expecting data? */
			f->uncompress.error = ue_WRONG_LENGTH;
			return false;
		}
		assert (f->uncompress.bz2.avail_in > 0 || f->len == 0);
	}
	if (f->uncompress.bz2.avail_in > 0 &&
			f->uncompress.bz2.next_in[0] == 'B') {

		/* might be concatenated files, so restart */
		ret = BZ2_bzDecompressEnd(&f->uncompress.bz2);
		if (ret != BZ_OK) {
			f->uncompress.error = ue_UNCOMPRESSION_ERROR;
			return false;
		}
		ret = BZ2_bzDecompressInit(&f->uncompress.bz2, 0, 0);
		if (ret != BZ_OK) {
			if (ret == BZ_MEM_ERROR) {
				f->error = ENOMEM;
				return false;
			}
			f->uncompress.error = ue_TRAILING_GARBAGE;
			f->uncompress.hadeos = true;
			return false;
		}
		f->uncompress.hadeos = false;
		/* successful restarted */
		return true;
	} else {
		/* mark End Of Stream, so bzDecompress is not called again */
		f->uncompress.hadeos = true;
		if (f->uncompress.bz2.avail_in > 0) {
			/* trailing garbage */
			f->uncompress.error = ue_TRAILING_GARBAGE;
			return false;
		} else
			/* normal end of stream, no error and
			 * no restart necessary: */
			return true;
	}
}


static inline int read_bz2(struct compressedfile *f, void *buffer, int size) {
	int ret;
	retvalue r;
	bool eoi;

	assert (f->compression == c_bzip2);
	assert (size >= 0);

	if (size == 0)
		return 0;

	f->uncompress.bz2.next_out = buffer;
	f->uncompress.bz2.avail_out = size;
	do {

		if (f->uncompress.bz2.avail_in == 0) {
			f->uncompress.available = 0;
			r = uncompression_read_internal_buffer(f);
			if (RET_WAS_ERROR(r)) {
				f->error = errno;
				return -1;
			}
			f->uncompress.bz2.next_in = (char*)f->uncompress.buffer;
			f->uncompress.bz2.avail_in = f->uncompress.available;
		}
		eoi = f->uncompress.bz2.avail_in == 0;

		ret = BZ2_bzDecompress(&f->uncompress.bz2);

		if (eoi && ret == BZ_OK &&
				f->uncompress.bz2.avail_out == (size_t)size) {
			/* if libbz2 does not detect an EndOfStream at the
			 * end of the file, let's fake an error: */
			ret = BZ_UNEXPECTED_EOF;
		}

		if (ret == BZ_STREAM_END) {
			size_t gotdata = size - f->uncompress.bz2.avail_out;

			f->uncompress.bz2.next_out = NULL;
			f->uncompress.bz2.avail_out = 0;

			if (!restart_bz2_if_needed(f))
				return -1;
			if (gotdata > 0 || f->uncompress.hadeos)
				return gotdata;

			/* read the restarted stream for data */
			ret = BZ_OK;
			f->uncompress.bz2.next_out = buffer;
			f->uncompress.bz2.avail_out = size;
		}

		/* repeat if no output was produced: */
	} while (ret == BZ_OK && f->uncompress.bz2.avail_out == (size_t)size);

	if (ret == BZ_OK) {
		return size - f->uncompress.bz2.avail_out;
	} else if (ret == BZ_MEM_ERROR) {
		fputs("Out of memory!", stderr);
		f->error = ENOMEM;
		return -1;
	} else {
		fprintf(stderr, "Error %d decompressing bz2 data\n", ret);
		f->uncompress.error = ue_UNCOMPRESSION_ERROR;
		return -1;
	}
	/* not reached */
}
#endif

#ifdef HAVE_LIBLZMA
static inline int read_lzma(struct compressedfile *f, void *buffer, int size) {
	int ret;
	retvalue r;
	bool eoi;

	assert (f->compression == c_lzma || f->compression == c_xz);
	assert (size >= 0);

	if (size == 0)
		return 0;

	f->uncompress.lzma.next_out = buffer;
	f->uncompress.lzma.avail_out = size;
	do {

		if (f->uncompress.lzma.avail_in == 0) {
			f->uncompress.available = 0;
			r = uncompression_read_internal_buffer(f);
			if (RET_WAS_ERROR(r)) {
				f->error = errno;
				return -1;
			}
			f->uncompress.lzma.next_in = f->uncompress.buffer;
			f->uncompress.lzma.avail_in = f->uncompress.available;
		}
		eoi = f->uncompress.lzma.avail_in == 0;

		ret = lzma_code(&f->uncompress.lzma, eoi?LZMA_FINISH:LZMA_RUN);

		if (eoi && ret == LZMA_OK &&
				f->uncompress.lzma.avail_out == (size_t)size) {
			/* not seen with liblzma, but still make sure this
			 * is treated as error (as with libbz2): */
			ret = LZMA_BUF_ERROR;
		}

		/* repeat if no output was produced: */
	} while (ret == LZMA_OK && f->uncompress.lzma.avail_out == (size_t)size);
	if (ret == LZMA_STREAM_END) {
		if (f->uncompress.lzma.avail_in > 0) {
			f->uncompress.error = ue_TRAILING_GARBAGE;
			return -1;
		} else if (f->len > 0) {
			f->uncompress.error = ue_WRONG_LENGTH;
			return -1;
		} else {
			/* check if this is the end of the file: */
			f->uncompress.available = 0;
			r = uncompression_read_internal_buffer(f);
			if (RET_WAS_ERROR(r)) {
				assert (f->error != 0);
				return -1;
			} else if (f->len != 0) {
				f->uncompress.error =
					(f->uncompress.available == 0)
					? ue_WRONG_LENGTH
					: ue_TRAILING_GARBAGE;
				return -1;
			}
		}
		f->uncompress.hadeos = true;
		return size - f->uncompress.lzma.avail_out;
	} else if (ret == LZMA_OK) {
		assert (size - f->uncompress.lzma.avail_out > 0);
		return size - f->uncompress.lzma.avail_out;
	} else if (ret == LZMA_MEM_ERROR) {
		fputs("Out of memory!", stderr);
		f->error = ENOMEM;
		return -1;
	} else {
		fprintf(stderr, "Error %d decompressing lzma data\n", ret);
		f->uncompress.error = ue_UNCOMPRESSION_ERROR;
		return -1;
	}
	/* not reached */
}
#endif

int uncompress_read(struct compressedfile *file, void *buffer, int size) {
	ssize_t s;

	if (file->external) {
		if (file->pipeinfd != -1) {
			/* things more complicated, as perhaps
			   something needs writing first... */
			return pipebackforth(file, buffer, size);
		}
		s = read(file->fd, buffer, size);
		if (s < 0)
			file->error = errno;
		return s;
	}

	assert (!file->external);

	if (file->error != 0 || file->uncompress.error != ue_NO_ERROR)
		return -1;

	/* libbz2 does not like being called after returning end of stream,
	 * so cache that: */
	if (file->uncompress.hadeos)
		return 0;

	switch (file->compression) {
		case c_none:
			if (file->len == 0)
				return 0;
			if (file->len > 0 && size > file->len)
				size = file->len;
			s = read(file->fd, buffer, size);
			if (s < 0)
				file->error = errno;
			file->len -= s;
			return s;
		case c_gzip:
			return read_gz(file, buffer, size);
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			return read_bz2(file, buffer, size);
#endif
#ifdef HAVE_LIBLZMA
		case c_xz:
		case c_lzma:
			return read_lzma(file, buffer, size);
#endif
		default:
			assert (false);
			return -1;
	}
}

static retvalue uncompress_commonclose(struct compressedfile *file, int *errno_p, const char **msg_p) {
	retvalue result;
	int ret;
	int e;
	pid_t pid;
	int status;
#define ERRORBUFFERSIZE 100
	static char errorbuffer[ERRORBUFFERSIZE];

	if (file == NULL)
		return RET_OK;

	if (file->external) {
		free(file->intermediate.buffer);
		(void)close(file->fd);
		if (file->pipeinfd != -1)
			(void)close(file->pipeinfd);
		file->fd = file->infd;
		file->infd = -1;
		result = RET_OK;
		if (file->pid <= 0)
			return RET_OK;
		pid = -1;
		do {
			if (interrupted()) {
				*errno_p = EINTR;
				*msg_p = "Interrupted";
				result = RET_ERROR_INTERRUPTED;
			}
			pid = waitpid(file->pid, &status, 0);
			e = errno;
		} while (pid == -1 && (e == EINTR || e == EAGAIN));
		if (pid == -1) {
			*errno_p = e;
			*msg_p = strerror(file->error);
			return RET_ERRNO(e);
		}
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == 0)
				return result;
			else {
				*errno_p = -EINVAL;
				snprintf(errorbuffer, ERRORBUFFERSIZE,
					"%s exited with code %d",
					extern_uncompressors[file->compression],
					(int)(WEXITSTATUS(status)));
				*msg_p = errorbuffer;
				return RET_ERROR;
			}
		} else if (WIFSIGNALED(status) && WTERMSIG(status) != SIGUSR2) {
			*errno_p = -EINVAL;
			snprintf(errorbuffer, ERRORBUFFERSIZE,
					"%s killed by signal %d",
					extern_uncompressors[file->compression],
					(int)(WTERMSIG(status)));
			*msg_p = errorbuffer;
			return RET_ERROR;
		} else {
			*errno_p = -EINVAL;
			snprintf(errorbuffer, ERRORBUFFERSIZE,
					"%s failed",
					extern_uncompressors[file->compression]);
			*msg_p = errorbuffer;
			return RET_ERROR;
		}
		return result;
	}
	assert (!file->external);

	if (file->error != 0) {
		*errno_p = file->error;
		*msg_p = strerror(file->error);
		result = RET_ERRNO(file->error);
	} else if (file->uncompress.error != ue_NO_ERROR) {
		*errno_p = -EINVAL;
		if (file->uncompress.error == ue_TRAILING_GARBAGE)
			*msg_p = "Trailing garbage after compressed data";
		else if (file->uncompress.error == ue_WRONG_LENGTH)
			*msg_p = "Compressed data of unexpected length";
		else
			*msg_p = "Uncompression error";
		result = RET_ERROR;
	} else
		result = RET_OK;

	free(file->uncompress.buffer);
	file->uncompress.buffer = NULL;

	switch (file->compression) {
		case c_none:
			return result;
		case c_gzip:
			ret = inflateEnd(&file->uncompress.gz);
			if (RET_WAS_ERROR(result))
				return result;
			if (ret != Z_OK) {
				*errno_p = -EINVAL;
				if (file->uncompress.gz.msg) {
					/* static string if set: */
					*msg_p = file->uncompress.gz.msg;
				} else {
					*msg_p =
"zlib status in inconsistent state at inflateEnd";
				}
				return RET_ERROR_Z;
			}
			return RET_OK;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			ret = BZ2_bzDecompressEnd(&file->uncompress.bz2);
			if (RET_WAS_ERROR(result))
				return result;
			if (ret != BZ_OK) {
				*errno_p = -EINVAL;
				*msg_p = "Uncompression error";
				return RET_ERROR_BZ2;
			}
			return RET_OK;
#endif
#ifdef HAVE_LIBLZMA
		case c_lzma:
		case c_xz:
			lzma_end(&file->uncompress.lzma);
			if (RET_WAS_ERROR(result))
				return result;
			return RET_OK;
#endif
		default:
			assert (file->external);
			assert (false);
			return RET_ERROR_INTERNAL;
	}
	/* not reached */
}

/* check if there has been an error yet for this stream */
retvalue uncompress_error(struct compressedfile *file) {
	int e, status;
	pid_t pid;

	if (file == NULL)
		return RET_NOTHING;

	if (file->error != 0) {
		fprintf(stderr, "Error %d uncompressing file '%s': %s\n",
				file->error, file->filename,
				strerror(file->error));
		return RET_ERRNO(file->error);
	}

	if (file->external) {
		if (file->pid <= 0)
			/* nothing running any more: no new errors possible */
			return RET_OK;
		pid = waitpid(file->pid, &status, WNOHANG);
		if (pid < 0) {
			e = errno;
			fprintf(stderr,
"Error %d looking for child %lu (a '%s'): %s\n", e,
					(long unsigned)file->pid,
					extern_uncompressors[file->compression],
					strerror(e));
			return RET_ERRNO(e);
		}
		if (pid != file->pid) {
			/* still running */
			return RET_OK;
		}
		file->pid = -1;
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == 0)
				return RET_OK;
			else {
				fprintf(stderr,
					"%s exited with code %d\n",
					extern_uncompressors[file->compression],
					(int)(WEXITSTATUS(status)));
				return RET_ERROR;
			}
		} else if (WIFSIGNALED(status) && WTERMSIG(status) != SIGUSR2) {
			fprintf(stderr,
					"%s killed by signal %d\n",
					extern_uncompressors[file->compression],
					(int)(WTERMSIG(status)));
			return RET_ERROR;
		} else {
			fprintf(stderr,
					"%s failed\n",
					extern_uncompressors[file->compression]);
			return RET_ERROR;
		}
	}
	assert (!file->external);
	if (file->uncompress.error != ue_NO_ERROR) {
		if (file->uncompress.error == ue_TRAILING_GARBAGE)
			fprintf(stderr,
"Trailing garbage after compressed data in %s",
					file->filename);
		else if (file->uncompress.error == ue_WRONG_LENGTH)
			fprintf(stderr,
"Compressed data of different length than expected in %s",
					file->filename);
		return RET_ERROR;
	}
	return RET_OK;
}



void uncompress_abort(struct compressedfile *file) {
	pid_t pid;
	int e, status;

	if (file == NULL)
		return;

	if (file->external) {
		/* kill before closing, to avoid it getting
		 * a sigpipe */
		if (file->pid > 0)
			kill(file->pid, SIGTERM);
		if (file->infd >= 0)
			(void)close(file->infd);
		if (file->pipeinfd != -1)
			(void)close(file->pipeinfd);
		pid = -1;
		do {
			if (interrupted())
				break;
			pid = waitpid(file->pid, &status, 0);
			e = errno;
		} while (pid == -1 && (e == EINTR || e == EAGAIN));
		if (file->fd >= 0)
			(void)close(file->fd);
		if (pid != -1 && !(WIFEXITED(status)) && WIFSIGNALED(status)
				&& WTERMSIG(status) != SIGTERM
				&& WTERMSIG(status) != SIGUSR2) {
			fprintf(stderr, "%s killed by signal %d\n",
					extern_uncompressors[file->compression],
					(int)(WTERMSIG(status)));
		}
	} else {
		if (file->closefd && file->fd >= 0)
			(void)close(file->fd);
		switch (file->compression) {
			case c_none:
				break;
			case c_gzip:
				(void)inflateEnd(&file->uncompress.gz);
				memset(&file->uncompress.gz, 0,
						sizeof(file->uncompress.gz));
				break;
#ifdef HAVE_LIBBZ2
			case c_bzip2:
				(void)BZ2_bzDecompressEnd(&file->uncompress.bz2);
				memset(&file->uncompress.bz2, 0,
						sizeof(file->uncompress.bz2));
				break;
#endif
#ifdef HAVE_LIBLZMA
			case c_xz:
			case c_lzma:
				lzma_end(&file->uncompress.lzma);
				memset(&file->uncompress.lzma, 0,
						sizeof(file->uncompress.lzma));
				break;
#endif
			default:
				assert (file->external);
				break;
		}
		free(file->uncompress.buffer);
		file->uncompress.buffer = NULL;
	}
	free(file->filename);
	free(file);
}

retvalue uncompress_fdclose(struct compressedfile *file, int *errno_p, const char **msg_p) {
	retvalue r;

	assert(file->closefd == false);
	r = uncompress_commonclose(file, errno_p, msg_p);
	free(file);
	return r;
}

retvalue uncompress_close(struct compressedfile *file) {
	const char *msg;
	retvalue r;
	int e;

	if (file == NULL)
		return RET_OK;

	if (file->closefd)
		assert (file->filename != NULL);
	else
		assert (file->filename == NULL);

	r = uncompress_commonclose(file, &e, &msg);
	if (RET_IS_OK(r)) {
		if (file->closefd && file->fd >= 0 && close(file->fd) != 0) {
			e = errno;
			fprintf(stderr,
"Error %d reading from %s: %s!\n", e, file->filename, strerror(e));
			r = RET_ERRNO(e);
		}
		free(file->filename);
		free(file);
		return r;
	}
	if (file->closefd && file->fd >= 0)
		(void)close(file->fd);
	if (e == -EINVAL) {
		fprintf(stderr,
"Error reading from %s: %s!\n", file->filename, msg);
	} else {
		fprintf(stderr,
"Error %d reading from %s: %s!\n", e, file->filename, msg);
	}
	free(file->filename);
	free(file);
	return r;
}

enum compression compression_by_suffix(const char *name, size_t *len_p) {
	enum compression c;
	size_t len = *len_p;

	for (c = c_COUNT - 1 ; c > c_none ; c--) {
		size_t l = strlen(uncompression_suffix[c]);

		if (len <= l)
			continue;
		if (strncmp(name + len - l, uncompression_suffix[c], l) == 0) {
			*len_p -= l;
			return c;
		}
	}
	return c_none;
}

