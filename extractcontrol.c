/*  This file is part of "reprepro"
 *  Copyright (C) 2003 Bernhard R. Link
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
#include "debfile.h"

#ifdef HAVE_LIBARCHIVE
#error Why did this file got compiled instead of debfile.c?
#endif
// **********************************************************************
// * This is a very simple implementation calling ar and tar, which
// * is only used with --without-libarchive or when no libarchive was 
// * found.
// **********************************************************************

retvalue extractcontrol(char **control,const char *debfile) {
	int pipe1[2];
	int pipe2[2];
	int ret;
	pid_t ar,tar,pid;
	int status;
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
		gzFile f;
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
	
	/* avoid being a memory leak */
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
				if( !WIFEXITED(status) || 
						WEXITSTATUS(status) != 0) {
					fprintf(stderr,"Error from ar: %d\n",WEXITSTATUS(status));
					result = RET_ERROR;
				}
			} else if( pid == tar ) {
				tar = -1;
				if( !WIFEXITED(status) || 
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

retvalue getfilelist(/*@out@*/char **filelist, const char *debfile) {
	int pipe1[2];
	int pipe2[2];
	int ret;
	pid_t ar,tar,pid;
	int status;
	char *list = NULL;
	size_t listsize = 0;
	size_t len=0, last = 0;
	retvalue result;

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
		ret = execl("/usr/bin/ar","ar","p",debfile,"data.tar.gz",NULL);
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
		execl("/bin/tar","tar","-tzf","-",NULL);
		fprintf(stderr,"calling tar failed: %m\n");
		exit(254);
		
	}

	close(pipe1[0]);close(pipe1[1]);
	close(pipe2[1]);

	/* read data: */
	if( RET_IS_OK(result) ) do {
		ssize_t bytes_read;
		size_t ignore;

		if( listsize <= len + 512 ) {
			char *n;

			listsize = len + 1024;
			n = realloc(list, listsize);
			if( n == NULL ) {
				result = RET_ERROR_OOM;
				break;
			}
			list = n;
		}

		ignore = 0;
		bytes_read = read(pipe2[0], list+len, listsize-len-1);
		if( bytes_read < 0 ) {
			int e = errno;
			fprintf(stderr, "Error reading from pipe: %d=%s\n",
					e, strerror(e));
			result = RET_ERRNO(e);
			break;
		} else if( bytes_read == 0 )
			break;
		else while( bytes_read > 0 ) {
			if( list[len] == '\0' ) {
				fprintf(stderr, "Unexpected NUL character from tar while getting filelist from %s!\n",debfile);
				result = RET_ERROR;
				break;
			} else if( list[len] == '\n' ) {
				if( len > last+ignore && list[len-1] != '/' ) {
					list[len] = '\0';
					len++;
					bytes_read--;
					memmove(list+last, list+last+ignore,
						1+len-last-ignore);
					last = len-ignore;
				} else {
					len++;
					bytes_read--;
					ignore = len-last;
				}
			} else if( list[len] == '.' && len == last+ignore ) {
				len++; ignore++;
				bytes_read--;
			} else if( list[len] == '/' && len == last+ignore ) {
				len++; ignore++;
				bytes_read--;
			} else {
				len++;
				bytes_read--;
			}
		}
		if( ignore > 0 ) {
			if( len > last+ignore )
				len = last;
			else {
				memmove(list+last, list+last+ignore, 
						1+len-last-ignore);
				len -= ignore;
			}
		}
	} while( TRUE );
	if( len != last ) {
		fprintf(stderr, "WARNING: unterminated output from tar over pipe while extracting filelist of %s\n",debfile);
		list[len] = '\0';
		fprintf(stderr, "The item '%s' might got lost.\n",
				list+last);
		result = RET_ERROR;
	} else {
		char *n = realloc(list,len+1);
		if( n == NULL )
			result = RET_ERROR_OOM;
		else {
			list = n;
			list[len] = '\0';
		}
	}
	close(pipe2[0]);
	
	while( ar != -1 || tar != -1 ) {
		pid=wait(&status);
		if( pid < 0 ) {
			if( errno != EINTR )
				RET_UPDATE(result,RET_ERRNO(errno));
		} else {
			if( pid == ar ) {
				ar = -1;
				if( !WIFEXITED(status) || 
						WEXITSTATUS(status) != 0) {
					fprintf(stderr,"Error from ar: %d\n",WEXITSTATUS(status));
					result = RET_ERROR;
				}
			} else if( pid == tar ) {
				tar = -1;
				if( !WIFEXITED(status) || 
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
		*filelist = list;
	else
		free(list);
	return result;
}
