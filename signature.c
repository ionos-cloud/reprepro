/*  This file is part of "reprepro"
 *  Copyright (C) 2003 Bernhard R. Link
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
	if( err ) {
		fprintf(stderr,"gpgme gave error: %s\n",gpgme_strerror(err));
		return RET_ERROR_GPGME;
	} else
		return RET_OK;
}

static retvalue signature_init(){
	GpgmeError err;

	if( context != NULL )
		return RET_NOTHING;
	err = gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP);
	if( err )
		return gpgerror(err);
	err = gpgme_new(&context);
	if( err )
		return gpgerror(err);
	err = gpgme_set_protocol(context,GPGME_PROTOCOL_OpenPGP);
	if( err )
		return gpgerror(err);
	gpgme_set_armor(context,1);
	return RET_OK;
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
				default:
					fprintf(stderr,"Error checking!\n");
					break;
			}
		}
		if( status == GPGME_SIG_STAT_GOOD ) {
			size_t fl,kl;

			if( key == NULL || (
				(kl = strlen(key)) <= (fl = strlen(fingerprint))
				&& strncmp(fingerprint+fl-kl,key,kl) == 0 )) {

				if( verbose > 0 )
				
				result = RET_OK;
				if( verbose <= 3 )
					break;
				continue;
			}
		}
	}
	if( result == RET_ERROR_BADSIG ) {
		fprintf(stderr,"NO VALID SIGNATURE with key '%s' found!\n",key);
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

	if( !release || !releasegpg )
		return RET_ERROR_OOM;

	r = signature_init();
	if( RET_WAS_ERROR(r) )
		return r;

	//TODO: choose which key to check against?

	/* Read the file and its signature into memory: */

	//TODO: Use callbacks for file-reading to have readable errormessages?
	err = gpgme_data_new_from_file(&dh_gpg,releasegpg,1);
	if( err ) {
		return gpgerror(err);
	}
	err = gpgme_data_new_from_file(&dh,release,1);
	if( err ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}

	/* Verify the signature */
	
	err = gpgme_op_verify(context,dh_gpg,dh,&stat);
	gpgme_data_release(dh_gpg);
	gpgme_data_release(dh);
	if( err )
		return gpgerror(err);

	switch( stat ) {
		case GPGME_SIG_STAT_BAD:
		case GPGME_SIG_STAT_NOKEY:
		case GPGME_SIG_STAT_DIFF:
		case GPGME_SIG_STAT_GOOD:
			return checksignatures(context,options,releasegpg);
		default:
			fprintf(stderr,"Error checking signature!\n");
			return RET_ERROR_GPGME;
	}
}


retvalue signature_sign(const char *options,const char *filename) {
	retvalue r;
	char *sigfilename;
	GpgmeError err;
	GpgmeData dh,dh_gpg;
	int ret;

	r = signature_init();
	if( RET_WAS_ERROR(r) )
		return r;

	//TODO: speifiy which key to use...

	/* First calculate the filename of the signature */

	sigfilename = calc_addsuffix(filename,"gpg");
	if( !sigfilename ) {
		return RET_ERROR_OOM;
	}

	/* Then make sure it does not already exists */
	
	ret = unlink(sigfilename);
	if( ret != 0 && errno != ENOENT ) {
		fprintf(stderr,"Could not remove '%s' to prepare replacement: %m\n",sigfilename);
		free(sigfilename);
		return RET_ERROR;
	}

	// TODO: Supply our own read functions to get sensible error messages.
	err = gpgme_data_new(&dh_gpg);
	if( err ) {
		free(sigfilename);
		return gpgerror(err);
	}
	err = gpgme_data_new_from_file(&dh,filename,1);
	if( err ) {
		gpgme_data_release(dh_gpg);
		free(sigfilename);
		return gpgerror(err);
	}

	err = gpgme_op_sign(context,dh,dh_gpg,GPGME_SIG_MODE_DETACH);
	gpgme_data_release(dh);
	if( err ) {
		gpgme_data_release(dh_gpg);
		free(sigfilename);
		return gpgerror(err);
	} else {
		char *signature_data;
		size_t signature_len;
		int fd;

		signature_data = gpgme_data_release_and_get_mem(dh_gpg,&signature_len);
		if( signature_data == NULL ) {
			return RET_ERROR_OOM;
		}
		fd = creat(sigfilename,0777);
		if( fd < 0 ) {
			free(signature_data);
			free(sigfilename);
			return RET_ERRNO(errno);
		}
		ret = write(fd,signature_data,signature_len);
		free(signature_data);
		ret = close(fd);
		//TODO check return values...
	}
	if( verbose > 1 ) {
		fprintf(stderr,"Successfully created '%s'\n",sigfilename);
	}
	free(sigfilename);

	return r;
}

/* Read a single chunk from a file, that may be signed. */
// TODO: Think about ways to check the signature...
retvalue signature_readsignedchunk(const char *filename, char **chunkread) {
	const char *startofchanges,*endofchanges;
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
	if( err ) {
		return gpgerror(err);
	}
	err = gpgme_data_new(&dh);
	if( err ) {
		gpgme_data_release(dh_gpg);
		return gpgerror(err);
	}
	err = gpgme_op_verify(context,dh_gpg,dh,&stat); 
	if( err ) {
		gpgme_data_release(dh_gpg);
		gpgme_data_release(dh);
		return gpgerror(err);
	}
	switch( stat ) {
		case GPGME_SIG_STAT_NOSIG:
			if( verbose > -1 ) 
				fprintf(stderr,"Data seems not to be signed trying to use directly...\n");
			plain_data = gpgme_data_release_and_get_mem(dh_gpg,&plain_len);
			gpgme_data_release(dh);
			break;
		case GPGME_SIG_STAT_DIFF:
		case GPGME_SIG_STAT_NOKEY:
			if( verbose > -1 ) 
				fprintf(stderr,"Signature could not be checked or multiple signatures with different states, proceeding anyway...\n");
		case GPGME_SIG_STAT_GOOD:
			gpgme_data_release(dh_gpg);
			plain_data = gpgme_data_release_and_get_mem(dh,&plain_len);
			break;
		case GPGME_SIG_STAT_BAD:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"Signature is bad!\n");
			return RET_ERROR_BADSIG;
		default:
			gpgme_data_release(dh_gpg);
			gpgme_data_release(dh);
			fprintf(stderr,"Error checking the signature within '%s'!\n",filename);
			return RET_ERROR_BADSIG;
	}

	startofchanges = plain_data;
	while( startofchanges < plain_data+plain_len && 
			*startofchanges && isspace(*startofchanges)) {
		startofchanges++;
	}
	if( startofchanges >= plain_data+plain_len ) {
		fprintf(stderr,"Could only find spaces within '%s'!\n",filename);
		free(plain_data);
		return RET_ERROR;
	}
	endofchanges = startofchanges;
	// TODO check for double newline and complain if there are things after it, that are not spaces...
	// TODO: check that the len is finaly reached and no \0 before...

	chunk = strndup(startofchanges,plain_len-(startofchanges-plain_data));
	free(plain_data);
	if( chunk == NULL )
		return RET_ERROR_OOM;
	*chunkread = chunk;
	return RET_OK;
}
