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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <gpgme.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "dirs.h"
#include "names.h"
#include "md5sum.h"
#include "chunks.h"
#include "release.h"
#include "signature.h"

extern int verbose;

static gpgme_ctx_t context = NULL;

static retvalue gpgerror(gpgme_error_t err) {
	if( err != 0 ) {
		fprintf(stderr,"gpgme gave %s error: %s\n",
				gpgme_strsource(err), gpgme_strerror(err));
		if( gpgme_err_code(err) == GPG_ERR_ENOMEM )
			return RET_ERROR_OOM;
		else
			return RET_ERROR_GPGME;
	} else
		return RET_OK;
}

/* Quick&dirty passphrase asking */
static gpgme_error_t signature_getpassphrase(UNUSED(void *hook), const char *uid_hint, UNUSED(const char *info), int prev_was_bad, int fd) {
	char *msg;
	const char *p;

	msg = mprintf("%s needs a passphrase\nPlease enter passphrase%s:",
			(uid_hint!=NULL)?uid_hint:"key",
			(prev_was_bad!=0)?" again":"");
	if( msg == NULL )
		return gpgme_err_make(GPG_ERR_SOURCE_USER_1, GPG_ERR_ENOMEM);
	p = getpass(msg);
	write(fd, p, strlen(p));
	write(fd, "\n", 1);
	free(msg);
	return GPG_ERR_NO_ERROR;
}

retvalue signature_init(bool_t allowpassphrase){
	gpgme_error_t err;

	if( context != NULL )
		return RET_NOTHING;
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
	return RET_OK;
}

void signatures_done(void) {
	if( context != NULL ) {
		gpgme_release(context);
		context = NULL;
	}
}

static inline retvalue containskey(const char *key, const char *fingerprint) {

	size_t fl,kl;
	const char *keypart,*p;

	fl = strlen(fingerprint);

	keypart = key;
	while( TRUE ) {
		while( *keypart != '\0' && xisspace(*keypart) )
			keypart++;
		if( *keypart == '\0' )
			/* nothing more to check, so nothing fullfilled */
			return RET_NOTHING;
		p = keypart;
		while( *p != '\0' && !xisspace(*p) && *p != '|' )
			p++;
		kl = p-keypart;
		if( kl < 8 && !IGNORING("Ignoring","To ignore this",shortkeyid,"Too short keyid specified (less than 8 characters) in '%s'!\n",key)) {
			return RET_ERROR;
		}
		if( kl < fl && strncmp(fingerprint+fl-kl,keypart,kl) == 0 )
			return RET_OK;
		keypart = p;
		while( *keypart != '\0' && xisspace(*keypart) )
			keypart++;
		if( *keypart == '\0' )
			return RET_NOTHING;
		if( *keypart == '|' )
			keypart++;
		else {
			fprintf(stderr,"Space separated values in keyid '%s'!\n(Use | symbols to separate multiple possible keys!)\n",key);
			return RET_ERROR;
		}
	}
}

retvalue signature_check(const char *options, const char *releasegpg, const char *release) {
	retvalue r;
	gpgme_error_t err;
	int fd,gpgfd;
	gpgme_data_t dh,dh_gpg;
	gpgme_verify_result_t result;
	gpgme_signature_t s;

	if( release == NULL || releasegpg == NULL )
		return RET_ERROR_OOM;

	r = signature_init(FALSE);
	if( RET_WAS_ERROR(r) )
		return r;

	/* Read the file and its signature into memory: */

	gpgfd = open(releasegpg, O_RDONLY|O_NOCTTY);
	if( gpgfd < 0 ) {
		int e = errno;
		fprintf(stderr, "Error opening '%s': %s\n", releasegpg, strerror(e));
		return RET_ERRNO(e);
	}
	fd = open(release, O_RDONLY|O_NOCTTY);
	if( fd < 0 ) {
		int e = errno;
		close(gpgfd);
		fprintf(stderr, "Error opening '%s': %s\n", release, strerror(e));
		return RET_ERRNO(e);
	}
	err = gpgme_data_new_from_fd(&dh_gpg, gpgfd);
	if( err != 0 ) {
		close(gpgfd);close(fd);
		return gpgerror(err);
	}
	err = gpgme_data_new_from_fd(&dh, fd);
	if( err != 0 ) {
		gpgme_data_release(dh_gpg);
		close(gpgfd);close(fd);
		return gpgerror(err);
	}

	/* Verify the signature */

	err = gpgme_op_verify(context,dh_gpg,dh,NULL);
	gpgme_data_release(dh_gpg);
	gpgme_data_release(dh);
	close(gpgfd);close(fd);
	if( err != 0 )
		return gpgerror(err);

	result = gpgme_op_verify_result(context);
	if( result == NULL ) {
		fprintf(stderr,"Internal error communicating with libgpgme: no result record!\n\n");
		return RET_ERROR_GPGME;
	}
	for( s = result->signatures ; s != NULL ; s = s->next ) {
		r = containskey(options, s->fpr);
		if( RET_WAS_ERROR(r) )
			return r;
		if( r == RET_NOTHING )
			/* there's no use in checking signatures not sufficient */
			continue;
		assert( RET_IS_OK(r) );
		switch( gpgme_err_code(s->status) ) {
			case GPG_ERR_NO_ERROR:
				return RET_OK;
			case GPG_ERR_KEY_EXPIRED:
				fprintf(stderr,
"WARNING: valid signature in '%s' with '%s', which has been expired.\n"
"         as the key was manually specified it is still accepted!\n",
						releasegpg, s->fpr);
				return RET_OK;
			case GPG_ERR_CERT_REVOKED:
				if( verbose >= 0 ) {
					fprintf(stderr,
"WARNING\n"
"WARNING: valid signature in '%s' with '%s', which has been revoked.\n"
"WARNING: as the key was manually specified it is still accepted!\n"
"WARNING\n", releasegpg, s->fpr);
				}
				return RET_OK;
			case GPG_ERR_SIG_EXPIRED:
				if( verbose > 0 ) {
					fprintf(stderr,
"'%s' has a valid but expired signature with '%s'\n"
" signature created %s, expired %s\n",
						releasegpg, s->fpr,
						ctime(&s->timestamp),
						ctime(&s->exp_timestamp));
				}
				// not accepted:
				continue;
			case GPG_ERR_BAD_SIGNATURE:
				if( verbose > 0 ) {
					fprintf(stderr,
"WARNING: '%s' has a invalid signature with '%s'\n", releasegpg, s->fpr);
				}
				// not accepted:
				continue;
			case GPG_ERR_NO_PUBKEY:
				if( verbose > 0 ) {
					fprintf(stderr,
"Could not check validity of signature with '%s' in '%s' as public key missing!\n",
						s->fpr, releasegpg);
				}
				// not accepted:
				continue;
			case GPG_ERR_GENERAL:
				fprintf(stderr,
"gpgme returned an general error verifing signature with '%s' in '%s'!\n"
"Try running gpg --verify '%s' '%s' manually for hints what is happening.\n"
"If this does not print any errors, retry the command causing this message.\n",
						s->fpr, releasegpg,
						releasegpg, release);
				continue;
			/* there sadly no more is a way to make sure we have
			 * all possible ones handled */
			default:
				break;
		}
		fprintf(stderr,
"Error checking signature (gpgme returned unexpected value %d)!\n"
"Please file a bug report, so reprepro can handle this in the future.\n",
			gpgme_err_code(s->status));
		return RET_ERROR_GPGME;
	}

	return RET_NOTHING;
}

retvalue signature_sign(const char *options, const char *filename, const char *signaturename) {
	retvalue r;
	gpgme_error_t err;
	gpgme_data_t dh,dh_gpg;
	int ret;

	r = signature_init(FALSE);
	if( RET_WAS_ERROR(r) )
		return r;

	//TODO: specify which key to use...

	/* make sure it does not already exists */

	ret = unlink(signaturename);
	if( ret != 0 && errno != ENOENT ) {
		fprintf(stderr,"Could not remove '%s' to prepare replacement: %m\n",signaturename);
		return RET_ERROR;
	}

	assert(options != NULL);
	while( *options != '\0' && xisspace(*options) )
		options++;
	if( *options == '!' ) {
		// TODO: allow external programs, too
		fprintf(stderr,"'!' not allowed at start of signing options yet.\n");
		return RET_ERROR;
	} else if( strcasecmp(options,"yes") == 0 || strcasecmp(options,"default") == 0 ) {
		gpgme_signers_clear(context);
		options = NULL;
	} else {
		gpgme_key_t key;

		gpgme_signers_clear(context);
/*		does not work:
 		err = gpgme_get_key(context, options, &key, 1);
		if( gpgme_err_code(err) == GPG_ERR_AMBIGUOUS_NAME ) {
			fprintf(stderr, "'%s' is too ambiguous for gpgme!\n", options);
			return RET_ERROR;
		} else if( gpgme_err_code(err) == GPG_ERR_INV_VALUE ) {
			fprintf(stderr, "gpgme says '%s' is \"not a fingerprint or key ID\"!\n\n", options);
			return RET_ERROR;
		}
		if( err != 0 )
			return gpgerror(err);
		if( key == NULL ) {
			fprintf(stderr,"Could not find any key matching '%s'!\n",options);
			return RET_ERROR;
		}
*/
		err = gpgme_op_keylist_start(context, options, 1);
		if( err != 0 )
			return gpgerror(err);
		err = gpgme_op_keylist_next(context, &key);
		if( gpgme_err_code(err) == GPG_ERR_EOF ) {
			fprintf(stderr,"Could not find any key matching '%s'!\n",options);
			return RET_ERROR;
		}
		err = gpgme_signers_add(context,key);
		gpgme_key_unref(key);
		if( err != 0 ) {
			gpgme_op_keylist_end(context);
			return gpgerror(err);
		}
		gpgme_op_keylist_end(context);
	}

	// TODO: Supply our own read functions to get sensible error messages.
	err = gpgme_data_new(&dh_gpg);
	if( err != 0 ) {
		return gpgerror(err);
	}
	err = gpgme_data_new_from_file(&dh,filename,1);
	if( err != 0 ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}

	err = gpgme_op_sign(context,dh,dh_gpg,GPGME_SIG_MODE_DETACH);
	gpgme_data_release(dh);
	if( err != 0 ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	} else {
		char *signature_data;
		const char *p;
		size_t signature_len;
		ssize_t written;
		int fd;
		gpgme_sign_result_t signresult;

		signresult = gpgme_op_sign_result(context);
		if( signresult == NULL ) {
			fprintf(stderr,
"Error: gpgme returned NULL unexpectedly for gpgme_op_sign_result\n"
"This most likely means gpg is confused or produces some error libgpgme is\n"
"not able to understand. Try running gpg %s%s --output '%s' --detach-sign '%s'\n"
"for hints what this error might have been.\n",
				(options==NULL)?"":"-u ",
				(options==NULL)?"":options,
				signaturename, filename);
			gpgme_data_release(dh_gpg);
			return RET_ERROR_GPGME;
		}
		if( signresult->signatures == NULL ) {
			fprintf(stderr,
"Error: gpgme created no signature for '%s'!\n"
"This most likely means gpg is confused or produces some error libgpgme is\n"
"not able to understand. Try running gpg %s%s --output '%s' --detach-sign '%s'\n"
"for hints what this error might have been.\n",
				filename,
				(options==NULL)?"":"-u ",
				(options==NULL)?"":options,
				signaturename, filename);
			gpgme_data_release(dh_gpg);
			return RET_ERROR_GPGME;
		}

		signature_data = gpgme_data_release_and_get_mem(dh_gpg,&signature_len);
		if( signature_data == NULL ) {
			return RET_ERROR_OOM;
		}
		fd = open(signaturename, O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY|O_NOFOLLOW, 0666);
		if( fd < 0 ) {
			free(signature_data);
			return RET_ERRNO(errno);
		}
		p = signature_data;
		while( signature_len > 0 ) {
			written = write(fd,p,signature_len);
			if( written < 0 ) {
				int e = errno;
				fprintf(stderr, "Error writing to %s: %s\n",
						signaturename,
						strerror(e));
				free(signature_data);
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
			int e = errno;
			fprintf(stderr, "Error writing to %s: %s\n",
					signaturename,
					strerror(e));
			return RET_ERRNO(e);
		}
	}
	if( verbose > 1 ) {
		printf("Successfully created '%s'\n",signaturename);
	}

	return r;
}

/* retrieve a list of fingerprints of keys having signed (valid) or
 * which are mentioned in the signature (all). set broken if all signatures
 * was broken (hints to a broken file, as opposed to expired or whatever
 * else may make a signature invalid)). */
static retvalue checksigs(const char *filename, struct strlist *valid, struct strlist *all, bool_t *broken) {
	gpgme_verify_result_t result;
	gpgme_signature_t s;
	bool_t had_valid=FALSE, had_broken=FALSE;

	result = gpgme_op_verify_result(context);
	if( result == NULL ) {
		fprintf(stderr,"Internal error communicating with libgpgme: no result record!\n\n");
		return RET_ERROR_GPGME;
	}
	for( s = result->signatures ; s != NULL ; s = s->next ) {
		if( all != NULL ) {
			retvalue r = strlist_add_dup(all, s->fpr);
			if( RET_WAS_ERROR(r) )
				return r;
		}
		switch( gpgme_err_code(s->status) ) {
			case GPG_ERR_NO_ERROR:
				had_valid = TRUE;
				if( valid != NULL ) {
					retvalue r = strlist_add_dup(valid,
								s->fpr);
					if( RET_WAS_ERROR(r) )
						return r;
				}
				continue;
			case GPG_ERR_KEY_EXPIRED:
				had_valid = TRUE;
				if( verbose > 0 )
					fprintf(stderr,
"Ignoring signature with '%s' on '%s', as the key has expired.\n",
						s->fpr, filename);
				continue;
			case GPG_ERR_CERT_REVOKED:
				had_valid = TRUE;
				if( verbose > 0 )
					fprintf(stderr,
"Ignoring signature with '%s' on '%s', as the key is revoked.\n",
						s->fpr, filename);
				continue;
			case GPG_ERR_SIG_EXPIRED:
				had_valid = TRUE;
				if( verbose > 0 ) {
					fprintf(stderr,
"Ignoring signature with '%s' on '%s', as the signature has expired.\n"
" signature created %s, expired %s\n",
						s->fpr, filename,
						ctime(&s->timestamp),
						ctime(&s->exp_timestamp));
				}
				continue;
			case GPG_ERR_BAD_SIGNATURE:
				had_broken = TRUE;
				if( verbose > 0 ) {
					fprintf(stderr,
"WARNING: '%s' has a invalid signature with '%s'\n", filename, s->fpr);
				}
				continue;
			case GPG_ERR_NO_PUBKEY:
				if( verbose > 0 ) {
					fprintf(stderr,
"Could not check validity of signature with '%s' in '%s' as public key missing!\n",
						s->fpr, filename);
				}
				continue;
			case GPG_ERR_GENERAL:
				fprintf(stderr,
"gpgme returned an general error verifing signature with '%s' in '%s'!\n"
"Try running gpg --verify '%s' manually for hints what is happening.\n"
"If this does not print any errors, retry the command causing this message.\n",
						s->fpr, filename,
						filename);
				continue;
			/* there sadly no more is a way to make sure we have
			 * all possible ones handled */
			default:
				break;
		}
		fprintf(stderr,
"Error checking signature (gpgme returned unexpected value %d)!\n"
"Please file a bug report, so reprepro can handle this in the future.\n",
			gpgme_err_code(s->status));
		return RET_ERROR_GPGME;
	}
	if( broken != NULL && had_broken && ! had_valid )
		*broken = TRUE;
	return RET_OK;
}

/* Read a single chunk from a file, that may be signed. */
retvalue signature_readsignedchunk(const char *filename, char **chunkread, /*@null@*/ /*@out@*/struct strlist *validkeys, /*@null@*/ /*@out@*/ struct strlist *allkeys, bool_t *brokensignature) {
	const char *startofchanges,*endofchanges,*afterchanges;
	char *chunk;
	gpgme_error_t err;
	gpgme_data_t dh,dh_gpg;
	size_t plain_len;
	char *plain_data;
	retvalue r;
	struct strlist validfingerprints, allfingerprints;
	bool_t foundbroken = FALSE;

	strlist_init(&validfingerprints);
	strlist_init(&allfingerprints);

	r = signature_init(FALSE);
	if( RET_WAS_ERROR(r) )
		return r;

	/* I wished from_fd would actually read the data like this one does... */
	err = gpgme_data_new_from_file(&dh_gpg, filename, 1);
	if( err != 0 ) {
		return gpgerror(err);
	}
	err = gpgme_data_new(&dh);
	if( err != 0 ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}
	err = gpgme_op_verify(context,dh_gpg,NULL,dh);
	if( gpgme_err_code(err) == GPG_ERR_NO_DATA ) {
		if( verbose > -1 )
			fprintf(stderr,"Data seems not to be signed trying to use directly...\n");
		gpgme_data_release(dh);
		plain_data = gpgme_data_release_and_get_mem(dh_gpg,&plain_len);
	} else {
		if( err != 0 ) {
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			return gpgerror(err);
		}
		if( validkeys != NULL || allkeys != NULL || brokensignature != NULL ) {
			r = checksigs(filename,
				(validkeys!=NULL)?&validfingerprints:NULL,
				(allkeys!=NULL)?&allfingerprints:NULL,
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
"gpgme failed to extract the plain data out of '%s'.\n"
"While it did so in a way indicating running out of memory, experience says\n"
"this also happens when gpg returns a error code it does not understand.\n"
"To check this please try running gpg --output '%s' manually.\n",
					filename, filename);
			return RET_ERROR_GPGME;
		}
	}

	if( plain_data == NULL )
		r = RET_ERROR_OOM;
	else {
		startofchanges = plain_data;
		while( (size_t)(startofchanges - plain_data) < plain_len &&
				*startofchanges != '\0' && xisspace(*startofchanges)) {
			startofchanges++;
		}
		if( (size_t)(startofchanges - plain_data) >= plain_len ) {
			fprintf(stderr,
"Could only find spaces within '%s'!\n",
					filename);
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
					filename);
			else
				fprintf(stderr,
"Unexpected data after ending empty line in '%s'!\n",
					filename);
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
		if( validkeys != NULL )
			strlist_move(validkeys, &validfingerprints);
		if( allkeys != NULL )
			strlist_move(allkeys, &allfingerprints);
		if( brokensignature != NULL )
			*brokensignature = foundbroken;
	} else {
		if( validkeys != NULL )
			strlist_done(&validfingerprints);
		if( allkeys != NULL )
			strlist_done(&allfingerprints);
	}
	return r;
}
