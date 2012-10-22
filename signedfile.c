/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007,2009,2010 Bernhard R. Link
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
#endif /* HAVE_LIBGPGME */

static retvalue signature_sign(const struct strlist *options, const char *filename, void *data, size_t datalen, const char *signaturename, const char *clearsignfilename, bool willcleanup) {
	retvalue r;
	int ret, e;
#ifdef HAVE_LIBGPGME
	int i;
	gpg_error_t err;
	gpgme_data_t dh;
#endif /* HAVE_LIBGPGME */

	assert (options != NULL && options->count > 0);

	r = signature_init(false);
	if (RET_WAS_ERROR(r))
		return r;

	/* make sure it does not already exists */

	ret = unlink(signaturename);
	if (ret != 0 && (e = errno) != ENOENT) {
		fprintf(stderr,
"Could not remove '%s' to prepare replacement: %s\n",
				signaturename, strerror(e));
		return RET_ERROR;
	}
	ret = unlink(clearsignfilename);
	if (ret != 0 && (e = errno) != ENOENT) {
		fprintf(stderr,
"Could not remove '%s' to prepare replacement: %s\n",
				clearsignfilename, strerror(e));
		return RET_ERROR;
	}

	if (options->values[0][0] == '!') {
		// TODO: allow external programs, too
		fprintf(stderr,
"sign-scripts (starting with '!') not allowed yet.\n");
		return RET_ERROR;
	}
#ifdef HAVE_LIBGPGME
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
		e = errno;
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
#else /* HAVE_LIBGPGME */
	fprintf(stderr,
"ERROR: Cannot creature signatures as this reprepro binary is not compiled with support\n"
"for libgpgme.\n"); // TODO: "Only running external programs is supported.\n"
	return RET_ERROR_GPGME;
#endif /* HAVE_LIBGPGME */
}

struct signedfile {
	char *plainfilename, *newplainfilename;
	char *signfilename, *newsignfilename;
	char *inlinefilename, *newinlinefilename;
	retvalue result;
#define DATABUFFERUNITS (128ul * 1024ul)
	size_t bufferlen, buffersize;
	char *buffer;
};


retvalue signature_startsignedfile(const char *directory, const char *basefilename, const char *inlinefilename, struct signedfile **out) {
	struct signedfile *n;

	n = zNEW(struct signedfile);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	n->plainfilename = calc_dirconcat(directory, basefilename);
	if (FAILEDTOALLOC(n->plainfilename)) {
		free(n);
		return RET_ERROR_OOM;
	}
	n->inlinefilename = calc_dirconcat(directory, inlinefilename);
	if (FAILEDTOALLOC(n->inlinefilename)) {
		free(n->plainfilename);
		free(n);
		return RET_ERROR_OOM;
	}
	n->newplainfilename = NULL;
	n->bufferlen = 0;
	n->buffersize = DATABUFFERUNITS;
	n->buffer = malloc(n->buffersize);
	if (FAILEDTOALLOC(n->buffer)) {
		free(n->plainfilename);
		free(n->inlinefilename);
		free(n);
		return RET_ERROR_OOM;
	}
	*out = n;
	return RET_OK;
}

void signedfile_free(struct signedfile *f, bool cleanup) {
	if (f == NULL)
		return;
	if (f->newplainfilename != NULL) {
		if (cleanup)
			(void)unlink(f->newplainfilename);
		free(f->newplainfilename);
	}
	free(f->plainfilename);
	if (f->newsignfilename != NULL) {
		if (cleanup)
			(void)unlink(f->newsignfilename);
		free(f->newsignfilename);
	}
	free(f->signfilename);
	if (f->newinlinefilename != NULL) {
		if (cleanup)
			(void)unlink(f->newinlinefilename);
		free(f->newinlinefilename);
	}
	free(f->inlinefilename);
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

retvalue signedfile_prepare(struct signedfile *f, const struct strlist *options, bool willcleanup) {
	size_t len, ofs;
	int fd, ret;

	if (RET_WAS_ERROR(f->result))
		return f->result;

	/* write content to file */

	f->newplainfilename = calc_addsuffix(f->plainfilename, "new");
	if (FAILEDTOALLOC(f->newplainfilename))
		return RET_ERROR_OOM;

	(void)dirs_make_parent(f->newplainfilename);
	(void)unlink(f->newplainfilename);

	fd = open(f->newplainfilename, O_WRONLY|O_CREAT|O_TRUNC|O_NOCTTY, 0666);
	if (fd < 0) {
		int e = errno;
		fprintf(stderr, "Error creating file '%s': %s\n",
				f->newplainfilename,
				strerror(e));
		free(f->newplainfilename);
		f->newplainfilename = NULL;
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
					e, f->newplainfilename,
					strerror(e));
			(void)close(fd);
			(void)unlink(f->newplainfilename);
			free(f->newplainfilename);
			f->newplainfilename = NULL;
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
				e, f->newplainfilename,
				strerror(e));
		(void)unlink(f->newplainfilename);
		free(f->newplainfilename);
		f->newplainfilename = NULL;
		return RET_ERRNO(e);
	}
	/* now do the actual signing */
	if (options != NULL && options->count > 0) {
		retvalue r;

		assert (f->newplainfilename != NULL);
		f->signfilename = calc_addsuffix(f->plainfilename, "gpg");
		if (FAILEDTOALLOC(f->signfilename))
			return RET_ERROR_OOM;
		f->newsignfilename = calc_addsuffix(f->signfilename, "new");
		if (FAILEDTOALLOC(f->newsignfilename))
			return RET_ERROR_OOM;
		f->newinlinefilename = calc_addsuffix(f->inlinefilename, "new");
		if (FAILEDTOALLOC(f->newinlinefilename))
			return RET_ERROR_OOM;

		r = signature_sign(options,
				f->newplainfilename,
				f->buffer, f->bufferlen,
				f->newsignfilename,
				f->newinlinefilename,
				willcleanup);
		if (RET_WAS_ERROR(r))
			return r;
	}
	return RET_OK;
}

retvalue signedfile_finalize(struct signedfile *f, bool *toolate) {
	retvalue result = RET_OK, r;
	int e;

	if (f->newsignfilename != NULL && f->signfilename != NULL) {
		e = rename(f->newsignfilename, f->signfilename);
		if (e < 0) {
			e = errno;
			fprintf(stderr, "Error %d moving %s to %s: %s!\n", e,
					f->newsignfilename,
					f->signfilename, strerror(e));
			result = RET_ERRNO(e);
			/* after something was done, do not stop
			 * but try to do as much as possible */
			if (!*toolate)
				return result;
		} else {
			/* does not need deletion any more */
			free(f->newsignfilename);
			f->newsignfilename = NULL;
			*toolate = true;
		}
	}
	if (f->newinlinefilename != NULL && f->inlinefilename != NULL) {
		e = rename(f->newinlinefilename, f->inlinefilename);
		if (e < 0) {
			e = errno;
			fprintf(stderr, "Error %d moving %s to %s: %s!\n", e,
					f->newinlinefilename,
					f->inlinefilename, strerror(e));
			result = RET_ERRNO(e);
			/* after something was done, do not stop
			 * but try to do as much as possible */
			if (!*toolate)
				return result;
		} else {
			/* does not need deletion any more */
			free(f->newinlinefilename);
			f->newinlinefilename = NULL;
			*toolate = true;
		}
	}
	e = rename(f->newplainfilename, f->plainfilename);
	if (e < 0) {
		e = errno;
		fprintf(stderr, "Error %d moving %s to %s: %s!\n", e,
				f->newplainfilename,
				f->plainfilename, strerror(e));
		r = RET_ERRNO(e);
		RET_UPDATE(result, r);
	} else {
		free(f->newplainfilename);
		f->newplainfilename = NULL;
	}
	return result;
}
