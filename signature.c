/*  This file is part of "mirrorer" (TODO: find better title)
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
#include <getopt.h>
#include <string.h>
#include <malloc.h>
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

retvalue signature_check(const char *chunk, const char *releasegpg, const char *release) {
	retvalue r;
	char *releasecheck,*command;
	int ret;

	if( !release || !releasegpg )
		return RET_ERROR_OOM;

	r = chunk_getvalue(chunk,"ReleaseCheck",&releasecheck);
	/* if there is no command, then there is nothing to check... */
	if( RET_WAS_ERROR(r) || r == RET_NOTHING)
		return r;

	//TODO: note in documentation, that names should not contain
	// shell active characters...
	command = mprintf("%s %s %s",releasecheck,releasegpg,release);
	if( !command ) {
		free(releasecheck);
		return RET_ERROR_OOM;
	}

	//TODO: think about possible problems with spaces...
	ret = system(command);
	if( ret != 0 ) {
		fprintf(stderr,"Calling '%s' gave returncode %d!\n",command,ret);
		r = RET_ERROR;
	} else
		r = RET_OK;

	free(releasecheck);free(command);
	return r;
}


retvalue signature_sign(const char *chunk,const char *filename) {
	retvalue r;
	char *signwith,*sigfilename,*signcommand;
	int ret;
	
	r = chunk_getvalue(chunk,"SignWith",&signwith);
	/* in case of error or nothing to do there is nothing to do... */
	if( !RET_IS_OK(r) ) { 
		return r;
	}

	/* First calculate the filename of the signature */

	sigfilename = calc_addsuffix(filename,"gpg");
	if( !sigfilename ) {
		free(signwith);
		return RET_ERROR_OOM;
	}

	/* Then make sure it does not already exists */
	
	ret = unlink(sigfilename);
	if( ret != 0 && errno != ENOENT ) {
		fprintf(stderr,"Could not remove '%s' to prepare replacement: %m\n",sigfilename);
		free(sigfilename);
		return RET_ERROR;
	}

	/* calculate what to call to create it */
	
	signcommand = mprintf("%s %s %s",signwith,sigfilename,filename);
	free(signwith);
	free(sigfilename);

	if( !signcommand ) {
		return RET_ERROR_OOM;
	}

	//TODO: think about possible problems ...
	ret = system(signcommand);
	if( ret != 0 ) {
		fprintf(stderr,"Executing '%s' returned: %d\n",signcommand,ret);
		r = RET_ERROR;
	} else { 
		r = RET_OK;
	}
		
	free(signcommand);
	return r;
}

/* Read a single chunk from a file, that may be signed. */
// TODO: Think about ways to check the signature...
retvalue signature_readsignedchunk(const char *filename, char **chunkread) {
	char *chunk,*chunk2,*endmarker;
	gzFile f;
	int issigned = 0;
	
	f = gzopen(filename,"r");
	if( !f ) {
		fprintf(stderr,"Unable to open file %s: %m\n",filename);
		return RET_ERRNO(errno);
	}

	//TODO: why doesn't return this retvalue?
	chunk = chunk_read(f);
	if( !chunk )
		return RET_ERROR;

	if( strncmp(chunk,"-----BEGIN",10) == 0 ) {
		issigned = 1;
		// signed, do validations here...
		free(chunk);
		chunk = chunk_read(f);
		if( !chunk )
			return RET_ERROR;
	}

	if( issigned ) {
		endmarker = strstr(chunk,"-----");
		if( endmarker != NULL ) {
			if( verbose > 0 ) 
				fprintf(stderr,"Truncating at ----\n");
			*endmarker ='\0';
		}
	}

	while( (chunk2 = chunk_read(f)) != NULL ) {
		if( strncmp(chunk2,"-----",5) == 0 ) {
			if( !issigned ) {
				fprintf(stderr,"Unexpected ----\n");
			}
		} else {
				fprintf(stderr,"Unexpected extra data: '%s'\n",chunk2);
		}
		free(chunk2);
	}
	//TODO: check result:
	gzclose(f);

	*chunkread = chunk;
	return RET_OK;
}
