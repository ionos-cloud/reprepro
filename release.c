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
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <zlib.h>
#include <db.h>
#include "chunks.h"
#include "sources.h"
#include "md5sum.h"
#include "release.h"

extern int verbose;

/* check for a <filetocheck> to be have same md5sum and size as <nametocheck> in <releasefile>,
 * returns 1 if ok, == 0 if <nametocheck> not specified, != 1 on error */
int release_checkfile(const char *releasefile,const char *nametocheck,const char *filetocheck) {
	int r;
	gzFile fi;
	const char *f,*n;
	char *c,*files;
	char *filename,*md5andsize,*realmd5andsize;


	/* Get the md5sums from the Release-file */
	
	fi = gzopen(releasefile,"r");
	if( !fi ) {
		fprintf(stderr,"Error opening %s: %m!\n",releasefile);
		return 10;
	}
	c = chunk_read(fi);
	gzclose(fi);
	if( !c ) {
		fprintf(stderr,"Error reading %s.\n",releasefile);
		return 20;
	}
	f = chunk_getfield("MD5Sum",c);
	if( !f ){
		fprintf(stderr,"Missing MD5Sums-field.\n");
		free(c);
		return 5;
	}
	files = chunk_dupextralines(f);
	free(c);
	if( !files )
		return 30;
	
	realmd5andsize = NULL;
	if( (r=md5sum_and_size(&realmd5andsize,filetocheck,0)) != 0 ) {
		fprintf(stderr,"Error checking %s: %m\n",filetocheck);
		free(files);
		return 4;
	}

	n = files;
	while( ( r = sources_getfile(&n,&filename,&md5andsize) ) > 0 ) {
		if( verbose > 2 ) 
			fprintf(stderr,"is it %s?\n",filename);
		if( strcmp(filename,nametocheck) == 0 ) {
			if( verbose > 1 ) 
				fprintf(stderr,"found. is '%s' == '%s'?\n",md5andsize,realmd5andsize);
			if( strcmp(md5andsize,realmd5andsize) == 0 ) {
				if( verbose > 0 )
					printf("%s ok\n",nametocheck);
				free(md5andsize);free(filename);
				r = 1;
				break;
			} else {
				if( verbose >=0 )
					fprintf(stderr,"%s failed\n",nametocheck);
				free(md5andsize);free(filename);
				r = -1;
				break;
			}
		}
		free(md5andsize);free(filename);
	}
	if( r==0 && verbose>=0 )
		fprintf(stderr,"%s failed as missing\n",nametocheck);
	free(realmd5andsize);
	free(files);
	return r;
}
