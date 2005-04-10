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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include <zlib.h>
#include "error.h"
#include "md5sum.h"
#include "chunks.h"
#include "extractcontrol.h"

extern int verbose;

// **********************************************************************
// * This is simply a ugly prototype until I have time to get something
// * correct (perhaps borrowing code from dpkg2.0). Until then its just
// * a quick and dirty hack to make it running.
// **********************************************************************
//TODO: write this properly.


retvalue extractcontrol(char **control,const char *debfile) {
	int pipe1[2];
	int pipe2[2];
	int ret;
	pid_t ar,tar,pid;
	int status;
	gzFile f;
	char *controlchunk;

	retvalue result,r;

	result = RET_OK;

	ret = pipe(pipe1);
	if( ret < 0 ) {
		return RET_ERRNO(errno);
	}

	ret = pipe(pipe2);
	if( ret < 0 ) {
		close(pipe1[0]);close(pipe1[1]);
		return RET_ERRNO(errno);
	}
	
	ar = fork();
	if( ar < 0 ) {
		result = RET_ERRNO(errno);
		close(pipe1[0]);close(pipe1[1]);
		close(pipe2[0]);close(pipe2[1]);
		return result;
	}

	if( ar == 0 ) {
		/* calling ar */
		if( dup2(pipe1[1],1) < 0 )
			exit(255);
		close(pipe1[0]);close(pipe1[1]);
		close(pipe2[0]);close(pipe2[1]);
		//TODO without explicit path
		ret = execl("/usr/bin/ar","ar","p",debfile,"control.tar.gz",NULL);
		fprintf(stderr,"calling ar failed: %m\n");
		exit(254);
	}

	tar = fork();
	if( tar < 0 ) {
		result = RET_ERRNO(errno);
		close(pipe1[0]);close(pipe1[1]);
		close(pipe2[0]);close(pipe2[1]);
		tar = -1;
	} else if( tar == 0 ) {
		/* calling tar */
		if( dup2(pipe1[0],0) < 0 )
			exit(255);
		if( dup2(pipe2[1],1) < 0 )
			exit(255);
		close(pipe1[0]);close(pipe1[1]);
		close(pipe2[0]);close(pipe2[1]);
		//TODO without explicit path
		execl("/bin/tar","tar","-xOzf","-","./control",NULL);
		fprintf(stderr,"calling tar failed: %m\n");
		exit(254);
		
	}

	close(pipe1[0]);close(pipe1[1]);
	close(pipe2[1]);

	/* read data: */
	if( RET_IS_OK(result) ) {
		//TODO: making a gzdopen here is kinda stupid...
		f = gzdopen(pipe2[0],"r");
		if( f == NULL ) {
			fprintf(stderr,"Error opening gzip-stream for pipe!\n");
			RET_UPDATE(result,RET_ERROR);
		} else {
			r = chunk_read(f,&controlchunk);
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Got no control information from .deb!\n");
				r = RET_ERROR_MISSING;
			}
			RET_UPDATE(result,r);
			gzclose(f);
		}
	}
	
	/* avoid beeing a memory leak */
	if( !(RET_IS_OK(result)) )
		controlchunk = NULL;

	while( ar != -1 || tar != -1 ) {
		pid=wait(&status);
		if( pid < 0 ) {
			if( errno != EINTR )
				RET_UPDATE(result,RET_ERRNO(errno));
		} else {
			if( pid == ar ) {
				ar = -1;
				if( WIFEXITED(status) == 0 || 
						WEXITSTATUS(status) != 0) {
					fprintf(stderr,"Error from ar: %d\n",WEXITSTATUS(status));
					result = RET_ERROR;
				}
			} else if( pid == tar ) {
				tar = -1;
				if( WIFEXITED(status) == 0 || 
						WEXITSTATUS(status) != 0 ) {
					fprintf(stderr,"Error from tar: %d\n",WEXITSTATUS(status));
					result = RET_ERROR;
				}
			} else {
				// WTH?
				fprintf(stderr,"Who is %d, and why does this bother me?\n",pid);
			}
		}
		
	}
	if( RET_IS_OK(result) )
		*control = controlchunk;
	else
		free(controlchunk);
	return result;
}
