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

retvalue download_initialize(struct download **download,const char *config) {
	const char *m,*me,*nl;
	char *method;
	FILE *pipe;
	struct download *d;

	fprintf(stderr,"download_initialize: '%s'\n",config);
	m = config;
	while( *m && isblank(*m) )
		m++;
	nl = m;
	while( *nl && *nl != '\n' )
		nl++;
	me = nl;
	while( me > m && isblank(*me) )
		me--;
	if( me == m || !(method = strndup(m,me-m+1)) )
		return RET_ERROR_OOM;

	d = malloc(sizeof(struct download));
	if( !d ) {
		free(method);
		return RET_ERROR_OOM;
	}

	pipe = popen(method,"w");
	if( !pipe ) {
		fprintf(stderr,"Error executing '%s': %m\n",method);
		free(d);
		free(method);
		return RET_ERRNO(errno);
	}
		fprintf(stderr,"Executed '%s': %m\n",method);
	free(method);

	if( *nl ) {
		nl++;
		fwrite(nl,strlen(nl),1,pipe);
	}
	d->pipe = pipe;
	*download = d;
	
	return RET_OK;
}
retvalue download_add(struct download *download,const char *orig,const char *dest) {
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
