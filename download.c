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
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include "error.h"
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

	if( *config ) {
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
