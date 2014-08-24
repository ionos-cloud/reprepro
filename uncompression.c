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

#include "globals.h"
#include "error.h"
#include "mprintf.h"
#include "filecntl.h"
#include "uncompression.h"

const char * const uncompression_suffix[c_COUNT] = {
	"", ".gz", ".bz2", ".lzma", ".xz", ".lz" };

/* So help messages can hint what option to try */
const char * const uncompression_option[c_COUNT] = {
	NULL, NULL, "--bunzip2", "--unlzma", "--unxz", "--lunzip" };
/* how those are called in the config file */
const char * const uncompression_config[c_COUNT] = {
	".", ".gz", ".bz2", ".lzma", ".xz", ".lz" };


/*@null@*/ char *extern_uncompressors[c_COUNT] = {
	NULL, NULL, NULL, NULL, NULL};

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
	 * only check existance, if someone it putting files not executable
	 * by us there it is their fault (as being executeable by us is hard
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

/* check for existance of external programs */
void uncompressions_check(const char *gunzip, const char *bunzip2, const char *unlzma, const char *unxz, const char *lunzip) {
	search_binary(gunzip,  "gunzip",  &extern_uncompressors[c_gzip]);
	search_binary(bunzip2, "bunzip2", &extern_uncompressors[c_bzip2]);
	search_binary(unlzma,  "unlzma",  &extern_uncompressors[c_lzma]);
	search_binary(unxz,    "unxz",    &extern_uncompressors[c_xz]);
	search_binary(lunzip,  "lunzip",  &extern_uncompressors[c_lunzip]);
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
	struct intermediate_buffer {
		char *buffer;
		int ofs;
		int ready;
	} intermediate;
	union {
		gzFile gz;
#ifdef HAVE_LIBBZ2
		BZFILE *bz;
#endif
	};
};

static retvalue start_builtin(struct compressedfile *f, int *errno_p, const char **msg_p) {
	int fd;

	assert (f->compression != c_none);
	assert (uncompression_builtin(f->compression));

	switch (f->compression) {
		case c_gzip:
			// TODO: perhaps rather implement your own reading and
			// uncompression, this way length read cannot be controlled
			if (f->closefd) {
				fd = f->fd;
				f->fd = -1;
			} else
				fd = dup(f->fd);
			f->gz = gzdopen(dup(fd), "r");
			if (f->gz == NULL) {
				*errno_p = errno;
				*msg_p = strerror(errno);
				// TODO: better error message...
				fprintf(stderr,
"Error opening internal gz uncompression using zlib...\n");
				return RET_ERROR;
			}
			break;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			if (f->closefd) {
				fd = f->fd;
				f->fd = -1;
			} else
				fd = dup(f->fd);
			f->bz = BZ2_bzdopen(dup(fd), "r");
			if (f->bz == NULL) {
				*errno_p = errno;
				*msg_p = strerror(errno);
				// TODO: better error message...
				fprintf(stderr,
"Error opening internal bz2 uncompression using libbz2\n");
				return RET_ERROR;
			}
			break;
#endif
		default:
			assert (false);
	}
	return RET_OK;
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

int uncompress_read(struct compressedfile *file, void *buffer, int size) {
	ssize_t s;
	int i;

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
			i = gzread(file->gz, buffer, size);
			if (i < 0)
				file->error = errno;
			return i;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			i = BZ2_bzread(file->bz, buffer, size);
			if (i < 0)
				file->error = errno;
			return i;
#endif
		default:
			assert (false);
			return RET_ERROR_INTERNAL;
	}
}

static retvalue uncompress_commonclose(struct compressedfile *file, int *errno_p, const char **msg_p) {
	retvalue result;
	const char *msg;
	int zerror, e;
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

	switch (file->compression) {
		case c_none:
			if (file->error != 0) {
				*errno_p = file->error;
				*msg_p = strerror(file->error);
				return RET_ERRNO(file->error);
			} else
				return RET_OK;
		case c_gzip:
			file->fd = -1;
			msg = gzerror(file->gz, &zerror);
			if (zerror == Z_ERRNO) {
				*errno_p = file->error;
				(void)gzclose(file->gz);
				*msg_p = strerror(file->error);
				return RET_ERRNO(file->error);
			} else if (zerror < 0) {
				*errno_p = -EINVAL;
				snprintf(errorbuffer, ERRORBUFFERSIZE,
						"Zlib error %d: %s",
						zerror, msg);
				*msg_p = errorbuffer;
				(void)gzclose(file->gz);
				return RET_ERROR_Z;
			}
			zerror = gzclose(file->gz);
			if (zerror == Z_ERRNO) {
				*errno_p = file->error;
				*msg_p = strerror(file->error);
				return RET_ERRNO(file->error);
			} if (zerror < 0) {
				*errno_p = -EINVAL;
				snprintf(errorbuffer, ERRORBUFFERSIZE,
						"Zlib error %d", zerror);
				*msg_p = errorbuffer;
				return RET_ERROR_Z;
			} else
				return RET_OK;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			file->fd = -1;
			msg = BZ2_bzerror(file->bz, &zerror);
			if (zerror < 0) {
				*errno_p = -EINVAL;
				snprintf(errorbuffer, ERRORBUFFERSIZE,
						"libbz2 error %d: %s",
						zerror, msg);
				*msg_p = errorbuffer;
				BZ2_bzclose(file->bz);
				return RET_ERROR_BZ2;
			}
			/* no return value? does this mean no checksums? */
			BZ2_bzclose(file->bz);
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
	int e, zerror, status;
	const char *msg;
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
"Error looking for child %lu (a '%s'): %s\n",
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

	switch (file->compression) {
		case c_none:
			return RET_OK;
		case c_gzip:
			msg = gzerror(file->gz, &zerror);
			if (zerror >= 0)
				return RET_OK;
			if (zerror != Z_ERRNO) {
				fprintf(stderr,
"Zlib error %d uncompressing file '%s': %s\n",
						zerror,
						file->filename,
						msg);
				return RET_ERROR_Z;
			}
			return RET_ERROR;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			msg = BZ2_bzerror(file->bz, &zerror);
			if (zerror < 0) {
				fprintf(stderr,
"libbz2 error %d uncompressing file '%s': %s\n",
						zerror,
						file->filename,
						msg);
				return RET_ERROR_BZ2;
			} else
				return RET_OK;
#endif
		default:
			assert (file->external);
			return RET_ERROR_INTERNAL;
	}
	/* not reached */
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
	} else switch (file->compression) {
		case c_none:
			if (file->closefd && file->fd >= 0)
				(void)close(file->fd);
			break;
		case c_gzip:
			(void)gzclose(file->gz);
			break;
#ifdef HAVE_LIBBZ2
		case c_bzip2:
			BZ2_bzclose(file->bz);
			break;
#endif
		default:
			assert (file->external);
			break;
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

