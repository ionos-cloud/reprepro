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
#include <malloc.h>
#include <fcntl.h>

#include "signature_p.h"
#include "mprintf.h"
#include "strlist.h"
#include "dirs.h"
#include "names.h"
#include "chunks.h"
#include "release.h"
#include "readtextfile.h"

#ifdef HAVE_LIBGPGME
gpgme_ctx_t context = NULL;

retvalue gpgerror(gpg_error_t err) {
	if( err != 0 ) {
		fprintf(stderr, "gpgme gave error %s:%d:  %s\n",
				gpg_strsource(err), gpg_err_code(err),
				gpg_strerror(err));
		if( gpg_err_code(err) == GPG_ERR_ENOMEM )
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
	if( msg == NULL )
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

	if( context != NULL )
		return RET_NOTHING;
	gpgme_check_version(NULL);
	err = gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP);
	if( err != 0 )
		return gpgerror(err);
	err = gpgme_new(&context);
	if( err != 0 )
		return gpgerror(err);
	err = gpgme_set_protocol(context,GPGME_PROTOCOL_OpenPGP);
	if( err != 0 )
		return gpgerror(err);
	if( allowpassphrase )
		gpgme_set_passphrase_cb(context,signature_getpassphrase,NULL);
	gpgme_set_armor(context,1);
#endif /* HAVE_LIBGPGME */
	return RET_OK;
}

void signatures_done(void) {
#ifdef HAVE_LIBGPGME
	if( context != NULL ) {
		gpgme_release(context);
		context = NULL;
	}
#endif /* HAVE_LIBGPGME */
}

#ifdef HAVE_LIBGPGME
static retvalue check_signature_created(bool clearsign, bool willcleanup, /*@null@*/const struct strlist *options, const char *filename, const char *signaturename) {
	gpgme_sign_result_t signresult;
	char *uidoptions;
	int i;

	signresult = gpgme_op_sign_result(context);
	if( signresult != NULL && signresult->signatures != NULL )
		return RET_OK;
	/* in an ideal world, this point is never reached.
	 * Sadly it is and people are obviously confused by it,
	 * so do some work to give helpful messages. */
	if( options != NULL ) {
		assert (options->count > 0);
		uidoptions = mprintf(" -u '%s'", options->values[0]);
		for( i = 1 ;
		     uidoptions != NULL && i < options->count ;
		     i++ ) {
			char *u = mprintf("%s -u '%s'", uidoptions,
					options->values[0]);
			free(uidoptions);
			uidoptions = u;
		}
		if( FAILEDTOALLOC(uidoptions) )
			return RET_ERROR_OOM;
	} else
		uidoptions = NULL;

	if( signresult == NULL )
		fputs(
"Error: gpgme returned NULL unexpectedly for gpgme_op_sign_result\n", stderr);
	else
		fputs( "Error: gpgme created no signature!\n", stderr);
	fputs(
"This most likely means gpg is confused or produces some error libgpgme is\n"
"not able to understand. Try running\n", stderr);
	if( willcleanup )
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
	if( signature_data == NULL )
		return RET_ERROR_OOM;
	fd = open(signaturename, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY|O_NOFOLLOW, 0666);
	if( fd < 0 ) {
		free(signature_data);
		return RET_ERRNO(errno);
	}
	p = signature_data;
	while( signature_len > 0 ) {
		written = write(fd, p, signature_len);
		if( written < 0 ) {
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
	if( ret < 0 ) {
		e = errno;
		fprintf(stderr, "Error %d writing to %s: %s\n",
				e, signaturename,
				strerror(e));
		return RET_ERRNO(e);
	}
	if( verbose > 1 ) {
		printf("Successfully created '%s'\n", signaturename);
	}
	return RET_OK;
}

static retvalue create_signature(bool clearsign, gpgme_data_t dh, /*@null@*/const struct strlist *options, const char *filename, const char *signaturename, bool willcleanup) {
	gpg_error_t err;
	gpgme_data_t dh_gpg;
	retvalue r;

	err = gpgme_data_new(&dh_gpg);
	if( err != 0 )
		return gpgerror(err);
	err = gpgme_op_sign(context, dh, dh_gpg,
			clearsign?GPGME_SIG_MODE_CLEAR:GPGME_SIG_MODE_DETACH);
	if( err != 0 )
		return gpgerror(err);
	r = check_signature_created(clearsign, willcleanup,
			options, filename, signaturename);
	if( RET_WAS_ERROR(r) ) {
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

	assert( options != NULL && options->count > 0 );

	r = signature_init(false);
	if( RET_WAS_ERROR(r) )
		return r;

	/* make sure it does not already exists */

	ret = unlink(signaturename);
	if( ret != 0 && (e = errno) != ENOENT ) {
		fprintf(stderr, "Could not remove '%s' to prepare replacement: %s\n",
				signaturename, strerror(e));
		return RET_ERROR;
	}
	ret = unlink(clearsignfilename);
	if( ret != 0 && (e = errno) != ENOENT ) {
		fprintf(stderr, "Could not remove '%s' to prepare replacement: %s\n",
				clearsignfilename, strerror(e));
		return RET_ERROR;
	}

	if( options->values[0][0] == '!' ) {
		// TODO: allow external programs, too
		fprintf(stderr, "sign-scripts (starting with '!') not allowed yet.\n");
		return RET_ERROR;
	}
#ifdef HAVE_LIBGPGME
	gpgme_signers_clear(context);
	if( options->count == 1 &&
			(strcasecmp(options->values[0], "yes") == 0 ||
			  strcasecmp(options->values[0], "default") == 0) ) {
		/* use default options */
		options = NULL;
	} else for( i = 0 ; i < options->count ; i++ ) {
		const char *option = options->values[i];
		gpgme_key_t key;

		err = gpgme_op_keylist_start(context, option, 1);
		if( err != 0 )
			return gpgerror(err);
		err = gpgme_op_keylist_next(context, &key);
		if( gpg_err_code(err) == GPG_ERR_EOF ) {
			fprintf(stderr, "Could not find any key matching '%s'!\n", option);
			return RET_ERROR;
		}
		err = gpgme_signers_add(context, key);
		gpgme_key_unref(key);
		if( err != 0 ) {
			gpgme_op_keylist_end(context);
			return gpgerror(err);
		}
		gpgme_op_keylist_end(context);
	}

	err = gpgme_data_new_from_mem(&dh, data, datalen, 0);
	if( err != 0 ) {
		return gpgerror(err);
	}

	r = create_signature(false, dh, options,
			filename, signaturename, willcleanup);
	if( RET_WAS_ERROR(r) ) {
		gpgme_data_release(dh);
		return r;
	}
	i = gpgme_data_seek(dh, 0, SEEK_SET);
	if( i < 0 ) {
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
	if( RET_WAS_ERROR(r) )
		return r;
	return RET_OK;
#else /* HAVE_LIBGPGME */
	fprintf(stderr,
"ERROR: Cannot creature signatures as this reprepro binary is not compiled with support\n"
"for libgpgme.\n"); // TODO: "Only running external programs is supported.\n"
	return RET_ERROR_GPGME;
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
	if( result == NULL ) {
		fprintf(stderr,"Internal error communicating with libgpgme: no result record!\n\n");
		return RET_ERROR_GPGME;
	}
	if( signatures_p != NULL ) {
		count = 0;
		for( s = result->signatures ; s != NULL ; s = s->next ) {
			count++;
		}
		signatures = calloc(1, sizeof(struct signatures) +
				count * sizeof(struct signature));
		if( FAILEDTOALLOC(signatures) )
			return RET_ERROR_OOM;
		signatures->count = count;
		signatures->validcount = 0;
		sig = signatures->signatures;
	} else {
		signatures = NULL;
		sig = NULL;
	}
	for( s = result->signatures ; s != NULL ; s = s->next ) {
		enum signature_state state = sist_error;

		if( signatures_p != NULL ) {
			sig->keyid = strdup(s->fpr);
			if( FAILEDTOALLOC(sig->keyid) ) {
				signatures_free(signatures);
				return RET_ERROR_OOM;
			}
		}
		switch( gpg_err_code(s->status) ) {
			case GPG_ERR_NO_ERROR:
				had_valid = true;
				state = sist_valid;
				if( signatures )
					signatures->validcount++;
				break;
			case GPG_ERR_KEY_EXPIRED:
				had_valid = true;
				if( verbose > 0 )
					fprintf(stderr,
"Ignoring signature with '%s' on '%s', as the key has expired.\n",
						s->fpr, filename);
				state = sist_mostly;
				if( sig != NULL )
					sig->expired_key = true;
				break;
			case GPG_ERR_CERT_REVOKED:
				had_valid = true;
				if( verbose > 0 )
					fprintf(stderr,
"Ignoring signature with '%s' on '%s', as the key is revoked.\n",
						s->fpr, filename);
				state = sist_mostly;
				if( sig != NULL )
					sig->revoced_key = true;
				break;
			case GPG_ERR_SIG_EXPIRED:
				had_valid = true;
				if( verbose > 0 ) {
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
				if( sig != NULL )
					sig->expired_signature = true;
				break;
			case GPG_ERR_BAD_SIGNATURE:
				had_broken = true;
				if( verbose > 0 ) {
					fprintf(stderr,
"WARNING: '%s' has a invalid signature with '%s'\n", filename, s->fpr);
				}
				state = sist_bad;
				break;
			case GPG_ERR_NO_PUBKEY:
				if( verbose > 0 ) {
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
		if( state == sist_error ) {
			fprintf(stderr,
"Error checking signature (gpgme returned unexpected value %d)!\n"
"Please file a bug report, so reprepro can handle this in the future.\n",
				gpg_err_code(s->status));
			signatures_free(signatures);
			return RET_ERROR_GPGME;
		}
		if( sig != NULL ) {
			sig->state = state;
			sig++;
		}
	}
	if( broken != NULL && had_broken && ! had_valid )
		*broken = true;
	if( signatures_p != NULL )
		*signatures_p = signatures;
	return RET_OK;
}

static retvalue check_primary_keys(struct signatures *signatures) {
	/* Get the primary keys belonging to each signing key.
	   This might also invalidate a signature previously believed
	   valid if the primary key is expired */
	int i;

	for( i = 0 ; i < signatures->count ; i++ ) {
		gpg_error_t err;
		gpgme_key_t gpgme_key = NULL;
		gpgme_subkey_t subkey;
		struct signature *sig = &signatures->signatures[i];

		if( sig->state == sist_error || sig->state == sist_missing ) {
			sig->primary_keyid = strdup(sig->keyid);
			if( FAILEDTOALLOC(sig->primary_keyid) )
				return RET_ERROR_OOM;
			continue;
		}

		err = gpgme_get_key(context, sig->keyid, &gpgme_key, 0);
		if( err != 0 ) {
			fprintf(stderr, "gpgme error %s:%d retrieving key '%s': %s\n",
					gpg_strsource(err), (int)gpg_err_code(err),
					sig->keyid, gpg_strerror(err));
			if( gpg_err_code(err) == GPG_ERR_ENOMEM )
				return RET_ERROR_OOM;
			else
				return RET_ERROR_GPGME;
		}
		assert( gpgme_key != NULL );
		/* the first "sub"key is the primary key */
		subkey = gpgme_key->subkeys;
		if( subkey->revoked ) {
			sig->revoced_key = true;
			if( sig->state == sist_valid ) {
				sig->state = sist_mostly;
				signatures->validcount--;
			}
		}
		if( subkey->expired ) {
			sig->expired_key = true;
			if( sig->state == sist_valid ) {
				sig->state = sist_mostly;
				signatures->validcount--;
			}
		}
		sig->primary_keyid = strdup(subkey->keyid);
		gpgme_key_unref(gpgme_key);
		if( FAILEDTOALLOC(sig->primary_keyid) )
			return RET_ERROR_OOM;
	}
	return RET_OK;
}
#endif /* HAVE_LIBGPGME */

static inline void extractchunk(const char *buffer, const char **begin, const char **end, const char **next) {
	const char *startofchanges,*endofchanges,*afterchanges;

	startofchanges = buffer;
	while( *startofchanges == ' ' || *startofchanges == '\t' ||
			*startofchanges == '\r' || *startofchanges =='\n' )
		startofchanges++;

	endofchanges = startofchanges;
	afterchanges = NULL;
	while( *endofchanges != '\0' ) {
		if( *endofchanges == '\n' ) {
			endofchanges++;
			afterchanges = endofchanges;
			while( *afterchanges =='\r' )
				afterchanges++;
			if( *afterchanges == '\n' )
				break;
			endofchanges = afterchanges;
			afterchanges = NULL;
		} else
			endofchanges++;
	}

	if( afterchanges == NULL )
		afterchanges = endofchanges;
	else
		while( *afterchanges == '\n' || *afterchanges =='\r' )
			afterchanges++;
	*begin = startofchanges;
	*end = endofchanges;
	*next = afterchanges;
}

void signatures_free(struct signatures *signatures) {
	int i;

	if( signatures == NULL )
		return;

	for( i = 0 ; i < signatures->count ; i++ ) {
		free(signatures->signatures[i].keyid);
		free(signatures->signatures[i].primary_keyid);
	}
	free(signatures);
}

#ifdef HAVE_LIBGPGME
static retvalue extract_signed_data(const char *buffer, size_t bufferlen, const char *filenametoshow, char **chunkread, /*@null@*/ /*@out@*/struct signatures **signatures_p, bool *brokensignature, bool *failed) {
	const char *startofchanges,*endofchanges,*afterchanges;
	char *chunk;
	gpg_error_t err;
	gpgme_data_t dh,dh_gpg;
	size_t plain_len;
	char *plain_data;
	retvalue r;
	struct signatures *signatures = NULL;
	bool foundbroken = false;

	r = signature_init(false);
	if( RET_WAS_ERROR(r) )
		return r;

	err = gpgme_data_new_from_mem(&dh_gpg, buffer, bufferlen, 0);
	if( err != 0 )
		return gpgerror(err);

	err = gpgme_data_new(&dh);
	if( err != 0 ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}
	err = gpgme_op_verify(context, dh_gpg, NULL, dh);
	if( gpg_err_code(err) == GPG_ERR_NO_DATA ) {
		if( verbose > 5 )
			fprintf(stderr,"Data seems not to be signed trying to use directly....\n");
		gpgme_data_release(dh);
		gpgme_data_release(dh_gpg);
		return RET_NOTHING;
	} else {
		if( err != 0 ) {
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			return gpgerror(err);
		}
		if( signatures_p != NULL || brokensignature != NULL ) {
			r = checksigs(filenametoshow,
				(signatures_p!=NULL)?&signatures:NULL,
				(brokensignature!=NULL)?&foundbroken:NULL);
			if( RET_WAS_ERROR(r) ) {
				gpgme_data_release(dh_gpg);
				gpgme_data_release(dh);
				return r;
			}
		}
		gpgme_data_release(dh_gpg);
		plain_data = gpgme_data_release_and_get_mem(dh,&plain_len);
		if( plain_data == NULL ) {
			fprintf(stderr,
"(not yet fatal) ERROR: libgpgme failed to extract the plain data out of\n"
"'%s'.\n"
"While it did so in a way indicating running out of memory, experience says\n"
"this also happens when gpg returns a error code it does not understand.\n"
"To check this please try running gpg --verify '%s' manually.\n"
"Continuing extracting it ignoring all signatures...",
					filenametoshow, filenametoshow);
			*failed = true;
			signatures_free(signatures);
			return RET_NOTHING;
		}
		if( signatures != NULL ) {
			r = check_primary_keys(signatures);
			if( RET_WAS_ERROR(r) ) {
				signatures_free(signatures);
				return r;
			}
		}
	}

	if( plain_data == NULL )
		r = RET_ERROR_OOM;
	else {
		// TODO: check if the new extractchunk can be used...

		startofchanges = plain_data;
		while( (size_t)(startofchanges - plain_data) < plain_len &&
				*startofchanges != '\0' && xisspace(*startofchanges)) {
			startofchanges++;
		}
		if( (size_t)(startofchanges - plain_data) >= plain_len ) {
			fprintf(stderr,
"Could only find spaces within '%s'!\n",
					filenametoshow);
			r = RET_ERROR;
		} else
			r = RET_OK;
	}
	if( RET_IS_OK(r) ) {
		endofchanges = startofchanges;
		while( (size_t)(endofchanges - plain_data) < plain_len &&
				*endofchanges != '\0' &&
				( *endofchanges != '\n' || *(endofchanges-1)!= '\n')) {
			endofchanges++;
		}
		afterchanges = endofchanges;
		while( (size_t)(afterchanges - plain_data) < plain_len &&
				*afterchanges != '\0' && xisspace(*afterchanges)) {
			afterchanges++;
		}
		if( (size_t)(afterchanges - plain_data) != plain_len ) {
			if( *afterchanges == '\0' )
				fprintf(stderr,
"Unexpected \\0 character within '%s'!\n",
					filenametoshow);
			else
				fprintf(stderr,
"Unexpected data after ending empty line in '%s'!\n",
					filenametoshow);
			r = RET_ERROR;
		}
	}
	if( RET_IS_OK(r) ) {
		chunk = strndup(startofchanges,endofchanges-startofchanges);
		if( chunk == NULL )
			r = RET_ERROR_OOM;
		else
			*chunkread = chunk;
	}
#ifdef HAVE_GPGPME_FREE
	gpgme_free(plain_data);
#else
	free(plain_data);
#endif
	if( RET_IS_OK(r) ) {
		if( signatures_p != NULL )
			*signatures_p = signatures;
		if( brokensignature != NULL )
			*brokensignature = foundbroken;
	} else {
		signatures_free(signatures);
	}
	return r;
}
#endif /* HAVE_LIBGPGME */

/* Read a single chunk from a file, that may be signed. */
retvalue signature_readsignedchunk(const char *filename, const char *filenametoshow, char **chunkread, /*@null@*/ /*@out@*/struct signatures **signatures_p, bool *brokensignature) {
	char *chunk, *h, *afterchunk;
	const char *startofchanges,*endofchanges,*afterchanges;
	size_t chunklen, len;
	retvalue r;
	bool failed = false;

	r = readtextfile(filename, filenametoshow, &chunk, &chunklen);
	if( !RET_IS_OK(r) )
		return r;

	if( chunklen == 0 ) {
		fprintf(stderr, "Unexpected empty file '%s'!\n",
					filenametoshow);
		free(chunk);
		return RET_ERROR;
	}

	extractchunk(chunk, &startofchanges, &endofchanges, &afterchanges);
	if( endofchanges == startofchanges ) {
		fprintf(stderr, "Could only find spaces within '%s'!\n",
					filenametoshow);
		free(chunk);
		return RET_ERROR;
	}

	/* fast-track unsigned chunks: */
	if( startofchanges[0] != '-' && *afterchanges == '\0' ) {
		if( verbose > 5 )
			fprintf(stderr,"Data seems not to be signed trying to use directly...\n");
		len = chunk_extract(chunk, chunk, &afterchunk);
		assert( *afterchunk == '\0' );
		assert( chunk[len] == '\0' );
		h = realloc(chunk, len + 1);
		if( h != NULL )
			chunk = h;
		*chunkread = chunk;
		if( signatures_p != NULL )
			*signatures_p = NULL;
		if( brokensignature != NULL )
			*brokensignature = false;
		return RET_OK;
	}

	if( startofchanges[0] != '-' ) {
		fprintf(stderr, "Error parsing '%s': Seems not to be signed but has spurious empty line.\n", filenametoshow);
		free(chunk);
		return RET_ERROR;
	}

#ifdef HAVE_LIBGPGME
	r = extract_signed_data(chunk, chunklen, filenametoshow, chunkread,
			signatures_p, brokensignature, &failed);
	if( r != RET_NOTHING ) {
		free(chunk);
		return r;
	}
#endif

	/* We have no libgpgme, it failed, or could not find signature data,
	 * trying to extract it manually, ignoring signatures: */

	if( *afterchanges == '\0' ) {
		fprintf(stderr,
"First non-space character is a '-' but there is no empty line in\n"
"'%s'.\n"
"Unable to extract any data from it!\n", filenametoshow);
		free(chunk);
		return RET_ERROR;
	}
	if( strncmp(startofchanges, "-----BEGIN", 10) != 0 ) {
		fprintf(stderr,
"Strange content of '%s': First non-space character is '-',\n"
"but it does not begin with '-----BEGIN'.\n", filenametoshow);
		failed = true;
#ifndef HAVE_LIBGPGME
	} else {
		fprintf(stderr,
"Cannot check signatures from '%s' as compiled without support for libgpgme!\n"
"Extracting the content manually without looking at the signature...\n", filenametoshow);
#endif
	}

	len = chunk_extract(chunk, afterchanges, &afterchunk);

	if( len == 0 ) {
		fprintf(stderr,"Could not find any data within '%s'!\n",
				filenametoshow);
		free(chunk);
		return RET_ERROR;
	}

	if( *afterchunk == '\0' ) {
		const char *endmarker;

		endmarker = strstr(chunk, "\n-----");
		if( endmarker != NULL ) {
			endmarker++;
			assert( (size_t)(endmarker-chunk) < len );
			len = endmarker-chunk;
			chunk[len] = '\0';
		} else {
			fprintf(stderr,
"ERROR: Could not find end marker of signed data within '%s'.\n"
"Cannot determine what is data and what is not!\n",
					filenametoshow);
			free(chunk);
			return RET_ERROR;
		}
	} else if( strncmp(afterchunk, "-----", 5) != 0 ) {
		fprintf(stderr,"ERROR: Spurious empty line within '%s'.\n"
"Cannot determine what is data and what is not!\n",
				filenametoshow);
		free(chunk);
		return RET_ERROR;
	}

	assert( chunk[len] == '\0' );
	h = realloc(chunk, len + 1);
	if( h != NULL )
		chunk = h;
	if( signatures_p != NULL ) {
		/* pointer to structure with count 0 to make clear
		 * it is not unsigned */
		*signatures_p = calloc(1, sizeof(struct signatures));
		if( FAILEDTOALLOC(*signatures_p) ) {
			free(chunk);
			return RET_ERROR_OOM;
		}
	}
	*chunkread = chunk;
	if( brokensignature != NULL )
		*brokensignature = false;
	return RET_OK;
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

	n = calloc(1, sizeof(struct signedfile));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->plainfilename = calc_dirconcat(directory, basefilename);
	if( n->plainfilename == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}
	n->inlinefilename = calc_dirconcat(directory, inlinefilename);
	if( n->inlinefilename == NULL ) {
		free(n->plainfilename);
		free(n);
		return RET_ERROR_OOM;
	}
	n->newplainfilename = NULL;
	n->bufferlen = 0;
	n->buffersize = DATABUFFERUNITS;
	n->buffer = malloc(n->buffersize);
	if( FAILEDTOALLOC(n->buffer) ) {
		free(n->plainfilename);
		free(n->inlinefilename);
		free(n);
		return RET_ERROR_OOM;
	}
	*out = n;
	return RET_OK;
}

void signedfile_free(struct signedfile *f, bool cleanup) {
	if( f == NULL )
		return;
	if( f->newplainfilename != NULL ) {
		if( cleanup )
			(void)unlink(f->newplainfilename);
		free(f->newplainfilename);
	}
	free(f->plainfilename);
	if( f->newsignfilename != NULL ) {
		if( cleanup )
			(void)unlink(f->newsignfilename);
		free(f->newsignfilename);
	}
	free(f->signfilename);
	if( f->newinlinefilename != NULL ) {
		if( cleanup )
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
	if( RET_WAS_ERROR(f->result) )
		return;

	if( len > f->buffersize - f->bufferlen ) {
		size_t blocks = (len + f->bufferlen)/DATABUFFERUNITS;
		size_t newsize = (blocks + 1) * DATABUFFERUNITS;
		char *newbuffer;

		/* realloc is wasteful, but should not happen too often */
		newbuffer = realloc(f->buffer, newsize);
		if( newbuffer == NULL ) {
			free(f->buffer);
			f->buffer = NULL;
			f->result = RET_ERROR_OOM;
			return;
		}
		f->buffer = newbuffer;
		f->buffersize = newsize;
		assert( f->bufferlen < f->buffersize );
	}
	assert( len <= f->buffersize - f->bufferlen );
	memcpy(f->buffer + f->bufferlen, data, len);
	f->bufferlen += len;
	assert( f->bufferlen <= f->buffersize );
}

retvalue signedfile_prepare(struct signedfile *f, const struct strlist *options, bool willcleanup) {
	size_t len, ofs;
	int fd, ret;

	if( RET_WAS_ERROR(f->result) )
		return f->result;

	/* write content to file */

	f->newplainfilename = calc_addsuffix(f->plainfilename, "new");
	if( FAILEDTOALLOC(f->newplainfilename) )
		return RET_ERROR_OOM;

	(void)dirs_make_parent(f->newplainfilename);
	(void)unlink(f->newplainfilename);

	fd = open(f->newplainfilename, O_WRONLY|O_CREAT|O_TRUNC|O_NOCTTY, 0666);
	if( fd < 0 ) {
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
	while( len > 0 ) {
		ssize_t written;

		written = write(fd, f->buffer + ofs, len);
		if( written < 0 ) {
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
		assert( (size_t)written <= len );
		ofs += written;
		len -= written;
	}
	ret = close(fd);
	if( ret < 0 ) {
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
	if( options != NULL && options->count > 0 ) {
		retvalue r;

		assert( f->newplainfilename != NULL );
		f->signfilename = calc_addsuffix(f->plainfilename, "gpg");
		if( f->signfilename == NULL )
			return RET_ERROR_OOM;
		f->newsignfilename = calc_addsuffix(f->signfilename, "new");
		if( f->newsignfilename == NULL )
			return RET_ERROR_OOM;
		f->newinlinefilename = calc_addsuffix(f->inlinefilename, "new");
		if( FAILEDTOALLOC(f->newinlinefilename) )
			return RET_ERROR_OOM;

		r = signature_sign(options,
				f->newplainfilename,
				f->buffer, f->bufferlen,
				f->newsignfilename,
				f->newinlinefilename,
				willcleanup);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

retvalue signedfile_finalize(struct signedfile *f, bool *toolate) {
	retvalue result = RET_OK, r;
	int e;

	if( f->newsignfilename != NULL && f->signfilename != NULL ) {
		e = rename(f->newsignfilename, f->signfilename);
		if( e < 0 ) {
			e = errno;
			fprintf(stderr, "Error %d moving %s to %s: %s!\n", e,
					f->newsignfilename,
					f->signfilename, strerror(e));
			result = RET_ERRNO(e);
			/* after something was done, do not stop
			 * but try to do as much as possible */
			if( !*toolate )
				return result;
		} else {
			/* does not need deletion any more */
			free(f->newsignfilename);
			f->newsignfilename = NULL;
			*toolate = true;
		}
	}
	if( f->newinlinefilename != NULL && f->inlinefilename != NULL ) {
		e = rename(f->newinlinefilename, f->inlinefilename);
		if( e < 0 ) {
			e = errno;
			fprintf(stderr, "Error %d moving %s to %s: %s!\n", e,
					f->newinlinefilename,
					f->inlinefilename, strerror(e));
			result = RET_ERRNO(e);
			/* after something was done, do not stop
			 * but try to do as much as possible */
			if( !*toolate )
				return result;
		} else {
			/* does not need deletion any more */
			free(f->newinlinefilename);
			f->newinlinefilename = NULL;
			*toolate = true;
		}
	}
	e = rename(f->newplainfilename, f->plainfilename);
	if( e < 0 ) {
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
