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

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include "error.h"
#include "strlist.h"
#include "download.h"

struct download {
	FILE *pipe;
};

retvalue download_initialize(struct download **download,const char *method,const char *config) {
	FILE *pipe;
	struct download *d;

	fprintf(stderr,"download_initialize: '%s' '%s'\n",method,config);
	d = malloc(sizeof(struct download));
	if( !d ) {
		return RET_ERROR_OOM;
	}

	pipe = popen(method,"w");
	if( !pipe ) {
		fprintf(stderr,"Error executing '%s': %m\n",method);
		free(d);
		return RET_ERRNO(errno);
	}
		fprintf(stderr,"Executed '%s': %m\n",method);

	if( *config != '\0' ) {
		// TODO: check return
		fputs(config,pipe);
		fputc('\n',pipe);
	}
	d->pipe = pipe;
	*download = d;
	
	return RET_OK;
}
retvalue download_add(struct download *download,const char *orig,const char *dest) {
	/* this does not really belong here, but makes live easier... */
	if( !orig || !dest )
		return RET_ERROR_OOM; 

	fprintf(download->pipe,"%s %s\n",orig,dest);
	return RET_OK;
}
retvalue download_run(struct download *download) {
	int r;
	
	r = pclose(download->pipe);
	free(download);
	if( r == -1 ) {
		fprintf(stderr,"Error waiting for download to finish: %m\n");
		return RET_ERROR;
	}
	if( r == 0 )
		return RET_OK;
	else {
		fprintf(stderr,"Download-programm returned error: %d\n",r);
		return RET_ERROR;
	}
}

retvalue download_cancel(struct download *download) {
	int r;

	r = pclose(download->pipe);
	free(download);
	if( r == -1 ) {
		fprintf(stderr,"Error waiting for download to finish: %m\n");
		return RET_ERROR;
	}
	if( r == 0 )
		return RET_OK;
	else {
		fprintf(stderr,"Download-programm returned error: %d\n",r);
		return RET_ERROR;
	}
	return RET_NOTHING;
}

retvalue download_fetchfiles(const char *method,const char *config, 
		             struct strlist *todownload) {
	struct download *d;
	retvalue result,r;
	int i;

	assert( method != NULL && config != NULL && todownload != NULL && 
	 	todownload->count%2 == 0 );

	result = download_initialize(&d,method,config);
	if( RET_WAS_ERROR(result) )
		return result;
	for( i = 0 ; i+1 < todownload->count ; i+=2 ) {
		r = download_add(d,todownload->values[i],todownload->values[i+1]);
		RET_UPDATE(result,r);
		if( RET_WAS_ERROR(r) )
			break;
	}
	if( RET_WAS_ERROR(result) )
		r = download_cancel(d);
	else
		r = download_run(d);
	RET_UPDATE(result,r);
	return result;
}
