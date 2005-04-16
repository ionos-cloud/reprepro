/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <config.h>

#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
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

static GpgmeCtx context = NULL;

static retvalue gpgerror(GpgmeError err){
	if( err != GPGME_No_Error ) {
		fprintf(stderr,"gpgme gave error: %s\n",gpgme_strerror(err));
		return RET_ERROR_GPGME;
	} else
		return RET_OK;
}

static retvalue signature_init(void){
	GpgmeError err;

	if( context != NULL )
		return RET_NOTHING;
	err = gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP);
	if( err != GPGME_No_Error )
		return gpgerror(err);
	err = gpgme_new(&context);
	if( err != GPGME_No_Error )
		return gpgerror(err);
	err = gpgme_set_protocol(context,GPGME_PROTOCOL_OpenPGP);
	if( err != GPGME_No_Error )
		return gpgerror(err);
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
			fprintf(stderr,"Space seperated values in keyid '%s'!\n(Use | symbols to seperate multiple possible keys!)\n",key);
			return RET_ERROR;
		}
	}
}

static inline retvalue checksignatures(GpgmeCtx context,const char *key,const char *releasegpg) {
	int idx;
	GpgmeSigStat status;
	retvalue result = RET_ERROR_BADSIG;

	for( idx = 0 ; ; idx++){
		const char *fingerprint;
		fingerprint = gpgme_get_sig_status(context,idx,&status,NULL);
		if( fingerprint == NULL )
			break;
		if( verbose > 3 ) {
			fprintf(stderr,"gpgme '%s': ",fingerprint);
			switch( status ) {
				case GPGME_SIG_STAT_BAD:
					fprintf(stderr,"BAD SIGNATURE!\n");
					break;
				case GPGME_SIG_STAT_NOKEY:
					fprintf(stderr,"Unknown key!\n");
					break;
				case GPGME_SIG_STAT_GOOD:
					fprintf(stderr,"Good signature!\n");
					break;
#ifdef HASGPGMEGOODEXP
				case GPGME_SIG_STAT_GOOD_EXP:
					fprintf(stderr,"Valid but expired signature!\n");
					break;
				case GPGME_SIG_STAT_GOOD_EXPKEY:
					fprintf(stderr,"Valid signature with expired key!\n");
					break;
#endif
				default:
					fprintf(stderr,"Error checking (libgpgme returned %d)!\n",status);
					break;
			}
		}
#ifdef HASGPGMEGOODEXP
		/* The key is explicitly given, so we do not care for its age! */
		if( status == GPGME_SIG_STAT_GOOD || status == GPGME_SIG_STAT_GOOD_EXPKEY) {
#else
		if( status == GPGME_SIG_STAT_GOOD ) {
#endif
			retvalue r = (key==NULL) ? 
					RET_OK : 
					containskey(key,fingerprint);
			if( RET_IS_OK(r) ) {
				result = RET_OK;
				if( verbose <= 3 )
					break;
				continue;
			}
		}
	}
	if( result == RET_ERROR_BADSIG ) {
		fprintf(stderr,"NO VALID SIGNATURE with key '%s' found in '%s'!\n",key,releasegpg);
	} else if( result == RET_OK ) {
		if( verbose > 0 )
			fprintf(stderr,"Valid Signature within '%s' found.\n",releasegpg);
	}
	return result;
}

retvalue signature_check(const char *options, const char *releasegpg, const char *release) {
	retvalue r;
	GpgmeError err;
	GpgmeData dh,dh_gpg;
	GpgmeSigStat stat;

	if( release == NULL || releasegpg == NULL )
		return RET_ERROR_OOM;

	r = signature_init();
	if( RET_WAS_ERROR(r) )
		return r;

	/* Read the file and its signature into memory: */

	//TODO: Use callbacks for file-reading to have readable errormessages?
	err = gpgme_data_new_from_file(&dh_gpg,releasegpg,1);
	if( err != GPGME_No_Error ) {
		return gpgerror(err);
	}
	err = gpgme_data_new_from_file(&dh,release,1);
	if( err != GPGME_No_Error ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}

	/* Verify the signature */
	
	err = gpgme_op_verify(context,dh_gpg,dh,&stat);
	gpgme_data_release(dh_gpg);
	gpgme_data_release(dh);
	if( err != GPGME_No_Error )
		return gpgerror(err);

	switch( stat ) {
		case GPGME_SIG_STAT_BAD:
		case GPGME_SIG_STAT_NOKEY:
		case GPGME_SIG_STAT_DIFF:
		case GPGME_SIG_STAT_GOOD:
#ifdef HASGPGMEGOODEXP
		case GPGME_SIG_STAT_GOOD_EXP:
		case GPGME_SIG_STAT_GOOD_EXPKEY:
#endif
			return checksignatures(context,options,releasegpg);
		case GPGME_SIG_STAT_NOSIG:
			fprintf(stderr,"No signature found within '%s'!\n",releasegpg);
			return RET_ERROR_GPGME;
		case GPGME_SIG_STAT_NONE:
			fprintf(stderr,"gpgme returned an impossible condition for '%s'!\n"
"If you are using woody and there was no ~/.gnupg yet, try repeating the last command.\n"
,releasegpg);
			return RET_ERROR_GPGME;
		case GPGME_SIG_STAT_ERROR:
			fprintf(stderr,"gpgme reported errors checking '%s'!\n",releasegpg);
			return RET_ERROR_GPGME;
	}
	fprintf(stderr,"Error checking signature (gpg returned unexpected value %d)!\n"
			"try grep 'GPGPME_SIG_STAT.*%d' /usr/include/gpgme.h\n"
			"and do not forget to write a bugreport.\n",(int)stat,(int)stat);
	return RET_ERROR_GPGME;
}


retvalue signature_sign(const char *options, const char *filename, const char *signaturename) {
	retvalue r;
	GpgmeError err;
	GpgmeData dh,dh_gpg;
	int ret;

	r = signature_init();
	if( RET_WAS_ERROR(r) )
		return r;

	//TODO: specify which key to use...

	/* make sure it does not already exists */
	
	ret = unlink(signaturename);
	if( ret != 0 && errno != ENOENT ) {
		fprintf(stderr,"Could not remove '%s' to prepare replacement: %m\n",signaturename);
		return RET_ERROR;
	}

	// TODO: Supply our own read functions to get sensible error messages.
	err = gpgme_data_new(&dh_gpg);
	if( err != GPGME_No_Error ) {
		return gpgerror(err);
	}
	err = gpgme_data_new_from_file(&dh,filename,1);
	if( err != GPGME_No_Error ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}

	err = gpgme_op_sign(context,dh,dh_gpg,GPGME_SIG_MODE_DETACH);
	gpgme_data_release(dh);
	if( err != GPGME_No_Error ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	} else {
		char *signature_data;
		size_t signature_len;
		int fd;

		signature_data = gpgme_data_release_and_get_mem(dh_gpg,&signature_len);
		if( signature_data == NULL ) {
			return RET_ERROR_OOM;
		}
		fd = creat(signaturename,0777);
		if( fd < 0 ) {
			free(signature_data);
			return RET_ERRNO(errno);
		}
		ret = write(fd,signature_data,signature_len);
		free(signature_data);
		ret = close(fd);
		//TODO check return values...
	}
	if( verbose > 1 ) {
		fprintf(stderr,"Successfully created '%s'\n",signaturename);
	}

	return r;
}

/* Read a single chunk from a file, that may be signed. */
// TODO: Think about ways to check the signature...
retvalue signature_readsignedchunk(const char *filename, char **chunkread, bool_t onlyacceptsigned) {
	const char *startofchanges,*endofchanges,*afterchanges;
	char *chunk;
	GpgmeError err;
	GpgmeData dh,dh_gpg;
	GpgmeSigStat stat;
	size_t plain_len;
	char *plain_data;
	retvalue r;
	
	r = signature_init();
	if( RET_WAS_ERROR(r) )
		return r;

	err = gpgme_data_new_from_file(&dh_gpg,filename,1);
	if( err != GPGME_No_Error ) {
		return gpgerror(err);
	}
	err = gpgme_data_new(&dh);
	if( err != GPGME_No_Error ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}
	err = gpgme_op_verify(context,dh_gpg,dh,&stat); 
	if( err != GPGME_No_Error ) {
		gpgme_data_release(dh_gpg);
		gpgme_data_release(dh);
		return gpgerror(err);
	}
	switch( stat ) {
		case GPGME_SIG_STAT_NOSIG:
			if( onlyacceptsigned ) {
				gpgme_data_release(dh_gpg);
				gpgme_data_release(dh);
				fprintf(stderr,"No signature found in '%s'!\n",filename);
				return RET_ERROR_BADSIG;
			}
			if( verbose > -1 ) 
				fprintf(stderr,"Data seems not to be signed trying to use directly...\n");
			plain_data = gpgme_data_release_and_get_mem(dh_gpg,&plain_len);
			gpgme_data_release(dh);
			break;
		case GPGME_SIG_STAT_DIFF:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"Multiple signatures of different state, which is not yet supported in '%s'!\n",filename);
			return RET_ERROR_BADSIG;
		case GPGME_SIG_STAT_NOKEY:
			if( onlyacceptsigned ) {
				gpgme_data_release(dh_gpg);
				gpgme_data_release(dh);
				fprintf(stderr,"Unknown key involved in'%s'!\n",filename);
				return RET_ERROR_BADSIG;
			}
			if( verbose > -1 ) 
				fprintf(stderr,"Signature with unknown key, proceeding anyway...\n");
			gpgme_data_release(dh_gpg);
			plain_data = gpgme_data_release_and_get_mem(dh,&plain_len);
			break;
		case GPGME_SIG_STAT_GOOD:
			gpgme_data_release(dh_gpg);
			plain_data = gpgme_data_release_and_get_mem(dh,&plain_len);
			break;
		case GPGME_SIG_STAT_BAD:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"Signature is bad!\n");
			return RET_ERROR_BADSIG;
#ifdef HASGPGMEGOODEXP
		case GPGME_SIG_STAT_GOOD_EXP:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"Signature is valid but expired!\n");
			return RET_ERROR_BADSIG;
		case GPGME_SIG_STAT_GOOD_EXPKEY:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"Signature is valid but the key is expired!\n");
			return RET_ERROR_BADSIG;
#endif
		case GPGME_SIG_STAT_NONE:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"gpgme returned an impossible condition in '%s'!\n"
"If you are using woody and there was no ~/.gnupg yet, try repeating the last command.\n"
,filename);
			return RET_ERROR_GPGME;
		case GPGME_SIG_STAT_ERROR:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"gpgme reported an error checking '%s'!\n",filename);
			return RET_ERROR_GPGME;
		default:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"Error checking the signature within '%s' (gpgme gave error code %d)!\n",filename,(int)stat);
			return RET_ERROR_BADSIG;
	}

	startofchanges = plain_data;
	while( (size_t)(startofchanges - plain_data) < plain_len && 
			*startofchanges != '\0' && xisspace(*startofchanges)) {
		startofchanges++;
	}
	if( (size_t)(startofchanges - plain_data) >= plain_len ) {
		fprintf(stderr,"Could only find spaces within '%s'!\n",filename);
		free(plain_data);
		return RET_ERROR;
	}
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
		if( *afterchanges == '\0' ) {
			fprintf(stderr,"Unexpected \\0 character within '%s'!\n",filename);
			free(plain_data);
			return RET_ERROR;
		}
		fprintf(stderr,"Unexpected data after ending empty line in '%s'!\n",filename);
		free(plain_data);
		return RET_ERROR;
	}

	chunk = strndup(startofchanges,endofchanges-startofchanges);
	free(plain_data);
	if( chunk == NULL )
		return RET_ERROR_OOM;
	*chunkread = chunk;
	return RET_OK;
}
