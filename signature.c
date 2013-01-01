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
#include "readtextfile.h"

#ifdef HAVE_LIBGPGME
gpgme_ctx_t context = NULL;

retvalue gpgerror(gpg_error_t err) {
	if (err != 0) {
		fprintf(stderr, "gpgme gave error %s:%d:  %s\n",
				gpg_strsource(err), gpg_err_code(err),
				gpg_strerror(err));
		if (gpg_err_code(err) == GPG_ERR_ENOMEM)
			return RET_ERROR_OOM;
		else
			return RET_ERROR_GPGME;
	} else
		return RET_OK;
}

/* Quick&dirty passphrase asking */
static gpg_error_t signature_getpassphrase(UNUSED(void *hook), const char *uid_hint, UNUSED(const char *info), int prev_was_bad, int fd) {
	char *msg;
	const char *p;

	msg = mprintf("%s needs a passphrase\nPlease enter passphrase%s:",
			(uid_hint!=NULL)?uid_hint:"key",
			(prev_was_bad!=0)?" again":"");
	if (msg == NULL)
		return gpg_err_make(GPG_ERR_SOURCE_USER_1, GPG_ERR_ENOMEM);
	p = getpass(msg);
	write(fd, p, strlen(p));
	write(fd, "\n", 1);
	free(msg);
	return GPG_ERR_NO_ERROR;
}
#endif /* HAVE_LIBGPGME */

retvalue signature_init(bool allowpassphrase){
#ifdef HAVE_LIBGPGME
	gpg_error_t err;

	if (context != NULL)
		return RET_NOTHING;
	gpgme_check_version(NULL);
	err = gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP);
	if (err != 0)
		return gpgerror(err);
	err = gpgme_new(&context);
	if (err != 0)
		return gpgerror(err);
	err = gpgme_set_protocol(context, GPGME_PROTOCOL_OpenPGP);
	if (err != 0)
		return gpgerror(err);
	if (allowpassphrase)
		gpgme_set_passphrase_cb(context, signature_getpassphrase,
				NULL);
	gpgme_set_armor(context, 1);
#endif /* HAVE_LIBGPGME */
	return RET_OK;
}

void signatures_done(void) {
#ifdef HAVE_LIBGPGME
	if (context != NULL) {
		gpgme_release(context);
		context = NULL;
	}
#endif /* HAVE_LIBGPGME */
}

#ifdef HAVE_LIBGPGME
/* retrieve a list of fingerprints of keys having signed (valid) or
 * which are mentioned in the signature (all). set broken if all signatures
 * was broken (hints to a broken file, as opposed to expired or whatever
 * else may make a signature invalid)). */
static retvalue checksigs(const char *filename, struct signatures **signatures_p, bool *broken) {
	gpgme_verify_result_t result;
	gpgme_signature_t s;
	bool had_valid = false, had_broken = false;
	size_t count;
	struct signatures *signatures;
	struct signature *sig;

	result = gpgme_op_verify_result(context);
	if (result == NULL) {
		fprintf(stderr,
"Internal error communicating with libgpgme: no result record!\n\n");
		return RET_ERROR_GPGME;
	}
	if (signatures_p != NULL) {
		count = 0;
		for (s = result->signatures ; s != NULL ; s = s->next) {
			count++;
		}
		signatures = calloc(1, sizeof(struct signatures) +
				count * sizeof(struct signature));
		if (FAILEDTOALLOC(signatures))
			return RET_ERROR_OOM;
		signatures->count = count;
		signatures->validcount = 0;
		sig = signatures->signatures;
	} else {
		signatures = NULL;
		sig = NULL;
	}
	for (s = result->signatures ; s != NULL ; s = s->next) {
		enum signature_state state = sist_error;

		if (signatures_p != NULL) {
			sig->keyid = strdup(s->fpr);
			if (FAILEDTOALLOC(sig->keyid)) {
				signatures_free(signatures);
				return RET_ERROR_OOM;
			}
		}
		switch (gpg_err_code(s->status)) {
			case GPG_ERR_NO_ERROR:
				had_valid = true;
				state = sist_valid;
				if (signatures)
					signatures->validcount++;
				break;
			case GPG_ERR_KEY_EXPIRED:
				had_valid = true;
				if (verbose > 0)
					fprintf(stderr,
"Ignoring signature with '%s' on '%s', as the key has expired.\n",
						s->fpr, filename);
				state = sist_mostly;
				if (sig != NULL)
					sig->expired_key = true;
				break;
			case GPG_ERR_CERT_REVOKED:
				had_valid = true;
				if (verbose > 0)
					fprintf(stderr,
"Ignoring signature with '%s' on '%s', as the key is revoked.\n",
						s->fpr, filename);
				state = sist_mostly;
				if (sig != NULL)
					sig->revoced_key = true;
				break;
			case GPG_ERR_SIG_EXPIRED:
				had_valid = true;
				if (verbose > 0) {
					time_t timestamp = s->timestamp,
					      exp_timestamp = s->exp_timestamp;
					fprintf(stderr,
"Ignoring signature with '%s' on '%s', as the signature has expired.\n"
" signature created %s, expired %s\n",
						s->fpr, filename,
						ctime(&timestamp),
						ctime(&exp_timestamp));
				}
				state = sist_mostly;
				if (sig != NULL)
					sig->expired_signature = true;
				break;
			case GPG_ERR_BAD_SIGNATURE:
				had_broken = true;
				if (verbose > 0) {
					fprintf(stderr,
"WARNING: '%s' has a invalid signature with '%s'\n", filename, s->fpr);
				}
				state = sist_bad;
				break;
			case GPG_ERR_NO_PUBKEY:
				if (verbose > 0) {
					fprintf(stderr,
"Could not check validity of signature with '%s' in '%s' as public key missing!\n",
						s->fpr, filename);
				}
				state = sist_missing;
				break;
			case GPG_ERR_GENERAL:
				fprintf(stderr,
"gpgme returned an general error verifing signature with '%s' in '%s'!\n"
"Try running gpg --verify '%s' manually for hints what is happening.\n"
"If this does not print any errors, retry the command causing this message.\n",
						s->fpr, filename,
						filename);
				signatures_free(signatures);
				return RET_ERROR_GPGME;
			/* there sadly no more is a way to make sure we have
			 * all possible ones handled */
			default:
				break;
		}
		if (state == sist_error) {
			fprintf(stderr,
"Error checking signature (gpgme returned unexpected value %d)!\n"
"Please file a bug report, so reprepro can handle this in the future.\n",
				gpg_err_code(s->status));
			signatures_free(signatures);
			return RET_ERROR_GPGME;
		}
		if (sig != NULL) {
			sig->state = state;
			sig++;
		}
	}
	if (broken != NULL && had_broken && ! had_valid)
		*broken = true;
	if (signatures_p != NULL)
		*signatures_p = signatures;
	return RET_OK;
}

static retvalue check_primary_keys(struct signatures *signatures) {
	/* Get the primary keys belonging to each signing key.
	   This might also invalidate a signature previously believed
	   valid if the primary key is expired */
	int i;

	for (i = 0 ; i < signatures->count ; i++) {
		gpg_error_t err;
		gpgme_key_t gpgme_key = NULL;
		gpgme_subkey_t subkey;
		struct signature *sig = &signatures->signatures[i];

		if (sig->state == sist_error || sig->state == sist_missing) {
			sig->primary_keyid = strdup(sig->keyid);
			if (FAILEDTOALLOC(sig->primary_keyid))
				return RET_ERROR_OOM;
			continue;
		}

		err = gpgme_get_key(context, sig->keyid, &gpgme_key, 0);
		if (err != 0) {
			fprintf(stderr,
"gpgme error %s:%d retrieving key '%s': %s\n",
					gpg_strsource(err),
					(int)gpg_err_code(err),
					sig->keyid, gpg_strerror(err));
			if (gpg_err_code(err) == GPG_ERR_ENOMEM)
				return RET_ERROR_OOM;
			else
				return RET_ERROR_GPGME;
		}
		assert (gpgme_key != NULL);
		/* the first "sub"key is the primary key */
		subkey = gpgme_key->subkeys;
		if (subkey->revoked) {
			sig->revoced_key = true;
			if (sig->state == sist_valid) {
				sig->state = sist_mostly;
				signatures->validcount--;
			}
		}
		if (subkey->expired) {
			sig->expired_key = true;
			if (sig->state == sist_valid) {
				sig->state = sist_mostly;
				signatures->validcount--;
			}
		}
		sig->primary_keyid = strdup(subkey->keyid);
		gpgme_key_unref(gpgme_key);
		if (FAILEDTOALLOC(sig->primary_keyid))
			return RET_ERROR_OOM;
	}
	return RET_OK;
}
#endif /* HAVE_LIBGPGME */

void signatures_free(struct signatures *signatures) {
	int i;

	if (signatures == NULL)
		return;

	for (i = 0 ; i < signatures->count ; i++) {
		free(signatures->signatures[i].keyid);
		free(signatures->signatures[i].primary_keyid);
	}
	free(signatures);
}

#ifdef HAVE_LIBGPGME
static retvalue extract_signed_data(const char *buffer, size_t bufferlen, const char *filenametoshow, char **chunkread, /*@null@*/ /*@out@*/struct signatures **signatures_p, bool *brokensignature) {
	char *chunk;
	gpg_error_t err;
	gpgme_data_t dh, dh_gpg;
	size_t plain_len;
	char *plain_data;
	retvalue r;
	struct signatures *signatures = NULL;
	bool foundbroken = false;

	r = signature_init(false);
	if (RET_WAS_ERROR(r))
		return r;

	err = gpgme_data_new_from_mem(&dh_gpg, buffer, bufferlen, 0);
	if (err != 0)
		return gpgerror(err);

	err = gpgme_data_new(&dh);
	if (err != 0) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}
	err = gpgme_op_verify(context, dh_gpg, NULL, dh);
	if (gpg_err_code(err) == GPG_ERR_NO_DATA) {
		if (verbose > 5)
			fprintf(stderr,
"Data seems not to be signed trying to use directly....\n");
		gpgme_data_release(dh);
		gpgme_data_release(dh_gpg);
		return RET_NOTHING;
	} else {
		if (err != 0) {
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			return gpgerror(err);
		}
		if (signatures_p != NULL || brokensignature != NULL) {
			r = checksigs(filenametoshow,
				(signatures_p!=NULL)?&signatures:NULL,
				(brokensignature!=NULL)?&foundbroken:NULL);
			if (RET_WAS_ERROR(r)) {
				gpgme_data_release(dh_gpg);
				gpgme_data_release(dh);
				return r;
			}
		}
		gpgme_data_release(dh_gpg);
		plain_data = gpgme_data_release_and_get_mem(dh, &plain_len);
		if (plain_data == NULL) {
			fprintf(stderr,
"(not yet fatal) ERROR: libgpgme failed to extract the plain data out of\n"
"'%s'.\n"
"While it did so in a way indicating running out of memory, experience says\n"
"this also happens when gpg returns a error code it does not understand.\n"
"To check this please try running gpg --verify '%s' manually.\n"
"Continuing extracting it ignoring all signatures...",
					filenametoshow, filenametoshow);
			signatures_free(signatures);
			return RET_NOTHING;
		}
		if (signatures != NULL) {
			r = check_primary_keys(signatures);
			if (RET_WAS_ERROR(r)) {
				signatures_free(signatures);
				return r;
			}
		}
	}

	if (FAILEDTOALLOC(plain_data))
		r = RET_ERROR_OOM;
	else {
		size_t len;
		const char *afterchanges;

		chunk = malloc(plain_len + 1);
		len = chunk_extract(chunk, plain_data, plain_len, false,
				&afterchanges);
		if (len == 0) {
			fprintf(stderr,
"Could only find spaces within '%s'!\n",
					filenametoshow);
			free(chunk);
			r = RET_ERROR;
		} else if (afterchanges != plain_data + plain_len) {
			if (*afterchanges == '\0')
				fprintf(stderr,
"Unexpected \\0 character within '%s'!\n",
					filenametoshow);
			else
				fprintf(stderr,
"Unexpected data after ending empty line in '%s'!\n",
					filenametoshow);
			free(chunk);
			r = RET_ERROR;
		} else
			*chunkread = chunk;
	}
#ifdef HAVE_GPGPME_FREE
	gpgme_free(plain_data);
#else
	free(plain_data);
#endif
	if (RET_IS_OK(r)) {
		if (signatures_p != NULL)
			*signatures_p = signatures;
		if (brokensignature != NULL)
			*brokensignature = foundbroken;
	} else {
		signatures_free(signatures);
	}
	return r;
}
#endif /* HAVE_LIBGPGME */

/* Read a single chunk from a file, that may be signed. */
retvalue signature_readsignedchunk(const char *filename, const char *filenametoshow, char **chunkread, /*@null@*/ /*@out@*/struct signatures **signatures_p, bool *brokensignature) {
	char *chunk;
	const char *startofchanges, *afterchunk;
	const char *endmarker;
	size_t chunklen, len;
	retvalue r;

	r = readtextfile(filename, filenametoshow, &chunk, &chunklen);
	if (!RET_IS_OK(r))
		return r;

	if (chunklen == 0) {
		fprintf(stderr, "Unexpected empty file '%s'!\n",
					filenametoshow);
		free(chunk);
		return RET_ERROR;
	}

	startofchanges = chunk_getstart(chunk, chunklen, false);

	/* fast-track unsigned chunks: */
	if (startofchanges[0] != '-') {
		const char *afterchanges;

		len = chunk_extract(chunk, chunk, chunklen, false,
				&afterchanges);

		if (len == 0)  {
			fprintf(stderr,
"Could only find spaces within '%s'!\n",
					filenametoshow);
			free(chunk);
			return RET_ERROR;
		}
		if (*afterchanges != '\0') {
			fprintf(stderr,
"Error parsing '%s': Seems not to be signed but has spurious empty line.\n",
					filenametoshow);
			free(chunk);
			return RET_ERROR;
		}
		if (verbose > 5 && strncmp(chunk, "Format:", 7) != 0
				&& strncmp(chunk, "Source:", 7) != 0)
			fprintf(stderr,
"Data seems not to be signed trying to use directly...\n");
		assert (chunk[len] == '\0');
		*chunkread = realloc(chunk, len + 1);
		if (FAILEDTOALLOC(*chunkread))
			*chunkread = chunk;
		if (signatures_p != NULL)
			*signatures_p = NULL;
		if (brokensignature != NULL)
			*brokensignature = false;
		return RET_OK;
	}

#ifdef HAVE_LIBGPGME
	r = extract_signed_data(chunk, chunklen, filenametoshow, chunkread,
			signatures_p, brokensignature);
	if (r != RET_NOTHING) {
		free(chunk);
		return r;
	}
#endif
	/* We have no libgpgme, it failed, or could not find signature data,
	 * trying to extract it manually, ignoring signatures: */

	if (strncmp(startofchanges, "-----BEGIN", 10) != 0) {
		fprintf(stderr,
"Strange content of '%s': First non-space character is '-',\n"
"but it does not begin with '-----BEGIN'.\n", filenametoshow);
		free(chunk);
		return RET_ERROR;
#ifndef HAVE_LIBGPGME
	} else {
		fprintf(stderr,
"Cannot check signatures from '%s' as compiled without support for libgpgme!\n"
"Extracting the content manually without looking at the signature...\n", filenametoshow);
#endif
	}
	startofchanges = chunk_over(startofchanges);

	len = chunk_extract(chunk, startofchanges, chunklen - (startofchanges - chunk),
			false, &afterchunk);

	if (len == 0) {
		fprintf(stderr, "Could not find any data within '%s'!\n",
				filenametoshow);
		free(chunk);
		return RET_ERROR;
	}

	endmarker = strstr(chunk, "\n-----");
	if (endmarker != NULL) {
		endmarker++;
		assert ((size_t)(endmarker-chunk) < len);
		len = endmarker-chunk;
		chunk[len] = '\0';
	} else if (*afterchunk == '\0') {
		fprintf(stderr,
"ERROR: Could not find end marker of signed data within '%s'.\n"
"Cannot determine what is data and what is not!\n",
filenametoshow);
		free(chunk);
		return RET_ERROR;
	} else if (strncmp(afterchunk, "-----", 5) != 0) {
		fprintf(stderr, "ERROR: Spurious empty line within '%s'.\n"
"Cannot determine what is data and what is not!\n",
				filenametoshow);
		free(chunk);
		return RET_ERROR;
	}

	assert (chunk[len] == '\0');
	if (signatures_p != NULL) {
		/* pointer to structure with count 0 to make clear
		 * it is not unsigned */
		*signatures_p = calloc(1, sizeof(struct signatures));
		if (FAILEDTOALLOC(*signatures_p)) {
			free(chunk);
			return RET_ERROR_OOM;
		}
	}
	*chunkread = realloc(chunk, len + 1);
	if (FAILEDTOALLOC(*chunkread))
		*chunkread = chunk;
	if (brokensignature != NULL)
		*brokensignature = false;
	return RET_OK;
}

