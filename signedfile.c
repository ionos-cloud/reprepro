/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2009,2010,2012 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>

#include "signature_p.h"
#include "mprintf.h"
#include "strlist.h"
#include "dirs.h"
#include "names.h"
#include "chunks.h"
#include "release.h"
#include "filecntl.h"
#include "hooks.h"

#ifdef HAVE_LIBGPGME
static retvalue check_signature_created(bool clearsign, bool willcleanup, /*@null@*/const struct strlist *options, const char *filename, const char *signaturename) {
	gpgme_sign_result_t signresult;
	char *uidoptions;
	int i;

	signresult = gpgme_op_sign_result(context);
	if (signresult != NULL && signresult->signatures != NULL)
		return RET_OK;
	/* in an ideal world, this point is never reached.
	 * Sadly it is and people are obviously confused by it,
	 * so do some work to give helpful messages. */
	if (options != NULL) {
		assert (options->count > 0);
		uidoptions = mprintf(" -u '%s'", options->values[0]);
		for (i = 1 ;
		     uidoptions != NULL && i < options->count ;
		     i++) {
			char *u = mprintf("%s -u '%s'", uidoptions,
					options->values[0]);
			free(uidoptions);
			uidoptions = u;
		}
		if (FAILEDTOALLOC(uidoptions))
			return RET_ERROR_OOM;
	} else
		uidoptions = NULL;

	if (signresult == NULL)
		fputs(
"Error: gpgme returned NULL unexpectedly for gpgme_op_sign_result\n", stderr);
	else
		fputs("Error: gpgme created no signature!\n", stderr);
	fputs(
"This most likely means gpg is confused or produces some error libgpgme is\n"
"not able to understand. Try running\n", stderr);
	if (willcleanup)
		fprintf(stderr,
"gpg %s --output 'some-other-file' %s 'some-file'\n",
			(uidoptions==NULL)?"":uidoptions,
			clearsign?"--clearsign":"--detach-sign");
	else
		fprintf(stderr,
"gpg %s --output '%s' %s '%s'\n",
			(uidoptions==NULL)?"":uidoptions,
			signaturename,
			clearsign?"--clearsign":"--detach-sign",
			filename);
	fputs(
"for hints what this error might have been. (Sometimes just running\n"
"it once manually seems also to help...)\n", stderr);
	return RET_ERROR_GPGME;
}

static retvalue signature_to_file(gpgme_data_t dh_gpg, const char *signaturename) {
	char *signature_data;
	const char *p;
	size_t signature_len;
	ssize_t written;
	int fd, e, ret;

	signature_data = gpgme_data_release_and_get_mem(dh_gpg, &signature_len);
	if (FAILEDTOALLOC(signature_data))
		return RET_ERROR_OOM;
	fd = open(signaturename, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY|O_NOFOLLOW, 0666);
	if (fd < 0) {
		free(signature_data);
		return RET_ERRNO(errno);
	}
	p = signature_data;
	while (signature_len > 0) {
		written = write(fd, p, signature_len);
		if (written < 0) {
			e = errno;
			fprintf(stderr, "Error %d writing to %s: %s\n",
					e, signaturename,
					strerror(e));
			free(signature_data);
			(void)close(fd);
			return RET_ERRNO(e);
		}
		signature_len -= written;
		p += written;
	}
#ifdef HAVE_GPGPME_FREE
	gpgme_free(signature_data);
#else
	free(signature_data);
#endif
	ret = close(fd);
	if (ret < 0) {
		e = errno;
		fprintf(stderr, "Error %d writing to %s: %s\n",
				e, signaturename,
				strerror(e));
		return RET_ERRNO(e);
	}
	if (verbose > 1) {
		printf("Successfully created '%s'\n", signaturename);
	}
	return RET_OK;
}

static retvalue create_signature(bool clearsign, gpgme_data_t dh, /*@null@*/const struct strlist *options, const char *filename, const char *signaturename, bool willcleanup) {
	gpg_error_t err;
	gpgme_data_t dh_gpg;
	retvalue r;

	err = gpgme_data_new(&dh_gpg);
	if (err != 0)
		return gpgerror(err);
	err = gpgme_op_sign(context, dh, dh_gpg,
			clearsign?GPGME_SIG_MODE_CLEAR:GPGME_SIG_MODE_DETACH);
	if (err != 0)
		return gpgerror(err);
	r = check_signature_created(clearsign, willcleanup,
			options, filename, signaturename);
	if (RET_WAS_ERROR(r)) {
		gpgme_data_release(dh_gpg);
		return r;
	}
	/* releases dh_gpg: */
	return signature_to_file(dh_gpg, signaturename);
}

static retvalue signature_sign(const struct strlist *options, const char *filename, void *data, size_t datalen, const char *signaturename, const char *clearsignfilename, bool willcleanup) {
	retvalue r;
	int i;
	gpg_error_t err;
	gpgme_data_t dh;

	assert (options != NULL && options->count > 0);
	assert (options->values[0][0] != '!');

	r = signature_init(false);
	if (RET_WAS_ERROR(r))
		return r;

	gpgme_signers_clear(context);
	if (options->count == 1 &&
			(strcasecmp(options->values[0], "yes") == 0 ||
			  strcasecmp(options->values[0], "default") == 0)) {
		/* use default options */
		options = NULL;
	} else for (i = 0 ; i < options->count ; i++) {
		const char *option = options->values[i];
		gpgme_key_t key;

		err = gpgme_op_keylist_start(context, option, 1);
		if (err != 0)
			return gpgerror(err);
		err = gpgme_op_keylist_next(context, &key);
		if (gpg_err_code(err) == GPG_ERR_EOF) {
			fprintf(stderr,
"Could not find any key matching '%s'!\n", option);
			return RET_ERROR;
		}
		err = gpgme_signers_add(context, key);
		gpgme_key_unref(key);
		if (err != 0) {
			gpgme_op_keylist_end(context);
			return gpgerror(err);
		}
		gpgme_op_keylist_end(context);
	}

	err = gpgme_data_new_from_mem(&dh, data, datalen, 0);
	if (err != 0) {
		return gpgerror(err);
	}

	r = create_signature(false, dh, options,
			filename, signaturename, willcleanup);
	if (RET_WAS_ERROR(r)) {
		gpgme_data_release(dh);
		return r;
	}
	i = gpgme_data_seek(dh, 0, SEEK_SET);
	if (i < 0) {
		int e = errno;
		fprintf(stderr,
"Error %d rewinding gpgme's data buffer to start: %s\n",
				e, strerror(e));
		gpgme_data_release(dh);
		return RET_ERRNO(e);
	}
	r = create_signature(true, dh, options,
			filename, clearsignfilename, willcleanup);
	gpgme_data_release(dh);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}
#endif /* HAVE_LIBGPGME */

static retvalue signature_with_extern(const struct strlist *options, const char *filename, const char *clearsignfilename, char **detachedfilename_p) {
	const char *clearsign;
	const char *detached;
	struct stat s;
	int status;
	pid_t child, found;
	const char *command;

	assert (options->count == 2);
	command = options->values[1];
	clearsign = (clearsignfilename == NULL)?"":clearsignfilename;
	detached = (*detachedfilename_p == NULL)?"":*detachedfilename_p;

	if (interrupted())
		return RET_ERROR_INTERRUPTED;

	if (lstat(filename, &s) != 0 || !S_ISREG(s.st_mode)) {
		fprintf(stderr, "Internal error: lost unsigned file '%s'?!\n",
				filename);
		return RET_ERROR;
	}

	child = fork();
	if (child == 0) {
		/* Try to close all open fd but 0,1,2 */
		closefrom(3);
		sethookenvironment(NULL, NULL, NULL, NULL);
		(void)execl(command, command, filename,
				clearsign, detached, ENDOFARGUMENTS);
		fprintf(stderr, "Error executing '%s' '%s' '%s' '%s': %s\n",
				command, filename, clearsign, detached,
				strerror(errno));
		_exit(255);
	}
	if (child < 0) {
		int e = errno;
		fprintf(stderr, "Error forking: %d=%s!\n", e, strerror(e));
		return RET_ERRNO(e);
	}
	errno = 0;
	while ((found = waitpid(child, &status, 0)) < 0) {
		int e = errno;
		if (e != EINTR) {
			fprintf(stderr,
"Error %d waiting for signing-command child %ld: %s!\n",
					e, (long)child, strerror(e));
			return RET_ERRNO(e);
		}
	}
	if (found != child) {
		fprintf(stderr,
"Confusing return value %ld from waitpid(%ld, ..., 0)", (long)found, (long)child);
		return RET_ERROR;
	}
	if (!WIFEXITED(status)) {
		fprintf(stderr,
"Error: Signing-hook '%s' called with arguments '%s' '%s' '%s' terminated abnormally!\n",
				command, filename, clearsign, detached);
		return RET_ERROR;
	}
	if (WEXITSTATUS(status) != 0) {
		fprintf(stderr,
"Error: Signing-hook '%s' called with arguments '%s' '%s' '%s' returned with exit code %d!\n",
				command, filename, clearsign, detached,
				(int)(WEXITSTATUS(status)));
		return RET_ERROR;
	}
	if (clearsignfilename != NULL) {
		if (lstat(clearsign, &s) != 0 || !S_ISREG(s.st_mode)) {
			fprintf(stderr,
"Error: Script '%s' did not generate '%s'!\n",
					command, clearsign);
			return RET_ERROR;
		} else if (s.st_size == 0) {
			fprintf(stderr,
"Error: Script '%s' created an empty '%s' file!\n",
					command, clearsign);
			return RET_ERROR;
		}
	}
	if (*detachedfilename_p != NULL) {
		if (lstat(detached, &s) != 0 || !S_ISREG(s.st_mode)) {
			/* no detached signature, no an error if there
			 * was a clearsigned file:*/
			if (clearsignfilename == NULL) {
				fprintf(stderr,
"Error: Script '%s' did not generate '%s'!\n",
						command, detached);
				return RET_ERROR;
			} else {
				if (verbose > 1)
					fprintf(stderr,
"Ignoring legacy detached signature '%s' not generated by '%s'\n",
							detached, command);
				detached = NULL;
				free(*detachedfilename_p);
				*detachedfilename_p = NULL;
			}
		} else if (s.st_size == 0) {
			fprintf(stderr,
"Error: Script '%s' created an empty '%s' file!\n",
					command, detached);
			return RET_ERROR;
		}
	}
	return RET_OK;
}

struct signedfile {
	retvalue result;
#define DATABUFFERUNITS (128ul * 1024ul)
	size_t bufferlen, buffersize;
	char *buffer;
};

retvalue signature_startsignedfile(struct signedfile **out) {
	struct signedfile *n;

	n = zNEW(struct signedfile);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->bufferlen = 0;
	n->buffersize = DATABUFFERUNITS;
	n->buffer = malloc(n->buffersize);
	if (FAILEDTOALLOC(n->buffer)) {
		free(n);
		return RET_ERROR_OOM;
	}
	*out = n;
	return RET_OK;
}

void signedfile_free(struct signedfile *f) {
	if (f == NULL)
		return;
	free(f->buffer);
	free(f);
	return;
}


/* store data into buffer */
void signedfile_write(struct signedfile *f, const void *data, size_t len) {

	/* no need to try anything if there already was an error */
	if (RET_WAS_ERROR(f->result))
		return;

	if (len > f->buffersize - f->bufferlen) {
		size_t blocks = (len + f->bufferlen)/DATABUFFERUNITS;
		size_t newsize = (blocks + 1) * DATABUFFERUNITS;
		char *newbuffer;

		/* realloc is wasteful, but should not happen too often */
		newbuffer = realloc(f->buffer, newsize);
		if (FAILEDTOALLOC(newbuffer)) {
			free(f->buffer);
			f->buffer = NULL;
			f->result = RET_ERROR_OOM;
			return;
		}
		f->buffer = newbuffer;
		f->buffersize = newsize;
		assert (f->bufferlen < f->buffersize);
	}
	assert (len <= f->buffersize - f->bufferlen);
	memcpy(f->buffer + f->bufferlen, data, len);
	f->bufferlen += len;
	assert (f->bufferlen <= f->buffersize);
}

retvalue signedfile_create(struct signedfile *f, const char *newplainfilename, char **newsignedfilename_p, char **newdetachedsignature_p, const struct strlist *options, bool willcleanup) {
	size_t len, ofs;
	int fd, ret;

	if (RET_WAS_ERROR(f->result))
		return f->result;

	/* write content to file */

	assert (newplainfilename != NULL);

	(void)dirs_make_parent(newplainfilename);
	(void)unlink(newplainfilename);

	fd = open(newplainfilename, O_WRONLY|O_CREAT|O_TRUNC|O_NOCTTY, 0666);
	if (fd < 0) {
		int e = errno;
		fprintf(stderr, "Error creating file '%s': %s\n",
				newplainfilename,
				strerror(e));
		return RET_ERRNO(e);
	}
	ofs = 0;
	len = f->bufferlen;
	while (len > 0) {
		ssize_t written;

		written = write(fd, f->buffer + ofs, len);
		if (written < 0) {
			int e = errno;
			fprintf(stderr, "Error %d writing to file '%s': %s\n",
					e, newplainfilename,
					strerror(e));
			(void)close(fd);
			return RET_ERRNO(e);
		}
		assert ((size_t)written <= len);
		ofs += written;
		len -= written;
	}
	ret = close(fd);
	if (ret < 0) {
		int e = errno;
		fprintf(stderr, "Error %d writing to file '%s': %s\n",
				e, newplainfilename,
				strerror(e));
		return RET_ERRNO(e);
	}
	/* now do the actual signing */
	if (options != NULL && options->count > 0) {
		retvalue r;
		const char *newsigned = *newsignedfilename_p;
		const char *newdetached = *newdetachedsignature_p;

		/* make sure the new files do not already exist: */
		if (unlink(newdetached) != 0 && errno != ENOENT) {
			fprintf(stderr,
"Could not remove '%s' to prepare replacement: %s\n",
					newdetached, strerror(errno));
			return RET_ERROR;
		}
		if (unlink(newsigned) != 0 && errno != ENOENT) {
			fprintf(stderr,
"Could not remove '%s' to prepare replacement: %s\n",
					newsigned, strerror(errno));
			return RET_ERROR;
		}
		/* if an hook is given, use that instead */
		if (options->values[0][0] == '!')
			r = signature_with_extern(options, newplainfilename,
					newsigned, newdetachedsignature_p);
		else
#ifdef HAVE_LIBGPGME
			r = signature_sign(options,
				newplainfilename,
				f->buffer, f->bufferlen,
				newdetached, newsigned,
				willcleanup);
#else /* HAVE_LIBGPGME */
			fputs(
"ERROR: Cannot creature signatures as this reprepro binary is not compiled\n"
"with support for libgpgme. (Only external signing using 'Signwith: !hook'\n"
"is supported.\n", stderr);
			return RET_ERROR_GPGME;
#endif
		if (RET_WAS_ERROR(r))
			return r;
	} else {
		/* no signatures requested */
		free(*newsignedfilename_p);
		*newsignedfilename_p = NULL;
		free(*newdetachedsignature_p);
		*newdetachedsignature_p = NULL;
	}
	return RET_OK;
}
