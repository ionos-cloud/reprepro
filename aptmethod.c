/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2004 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "md5sum.h"
#include "aptmethod.h"

extern int verbose;

struct queuedjob {
	struct queuedjob *next;
	char *command;
	size_t alreadywritten,len;
};

struct tobedone {
	struct tobedone *next;
	/* must be saved to know where is should be moved to: */
	char *uri;
	char *filename;
	/* if non-NULL, what is expected...*/
	char *md5sum;
};

struct aptmethod {
	struct aptmethod *next;
	char *name;
	char *baseuri;
	int stdin,stdout;
	pid_t child;

	enum { ams_waitforcapabilities=0, ams_ok, ams_failed } status;
	
	struct queuedjob *jobs,*lastqueued;
	struct tobedone *tobedone,*lasttobedone;
	char *inputbuffer;
	size_t len,read;
};

struct aptmethodrun {
	struct aptmethod *methods;
};

static void aptmethod_free(struct aptmethod *method) {
	if( method == NULL )
		return;
	free(method->name);
	free(method->baseuri);
	free(method->inputbuffer);

	while( method->jobs ) {
		struct queuedjob *job;

		job = method->jobs;
		method->jobs = job->next;

		free(job->command);
		free(job);
	}

	while( method->tobedone ) {
		struct tobedone *todo;

		todo = method->tobedone;
		method->tobedone = todo->next;

		free(todo->uri);
		free(todo->filename);
		free(todo->md5sum);
		free(todo);
	}
}

void aptmethod_cancel(struct aptmethodrun *run) {
	struct aptmethod *method,*next;

	if( run == NULL )
		return;
	method = run->methods;
	while( method ) {
		next = method->next;
		aptmethod_free(method);
		method = next;
	}
	free(run);
}

/******************Initialize the data structures***********************/

retvalue aptmethod_initialize_run(struct aptmethodrun **run) {
	struct aptmethodrun *r;

	r = calloc(1,sizeof(struct aptmethodrun));
	if( r == NULL )
		return RET_ERROR_OOM;
	else {
		*run = r;
		return RET_OK;
	}
}

retvalue aptmethod_newmethod(struct aptmethodrun *run,const char *uri,struct aptmethod **m) {
	struct aptmethod *method;
	const char *p;

	method = calloc(1,sizeof(struct aptmethod));
	if( method == NULL )
		return RET_ERROR_OOM;
	method->stdin = -1;
	method->stdout = -1;
	method->child = -1;
	method->status = ams_waitforcapabilities;
	p = uri;
	while( *p && ( *p == '_' || *p == '-' ||
		(*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
		(*p>='0' && *p<='9') ) ) {
		p++;
	}
	if( *p == '\0' ) {
		fprintf(stderr,"Did not find colon in method-URI '%s'!\n",uri);
		free(method);
		return RET_ERROR;
	}
	if( *p != ':' ) {
		fprintf(stderr,"Unexpected character '%c' in method-URI '%s'!\n",*p,uri);
		free(method);
		return RET_ERROR;
	}
	if( p == uri ) {
		fprintf(stderr,"Zero-length name in method-URI '%s'!\n",uri);
		free(method);
		return RET_ERROR;
	}

	method->name = strndup(uri,p-uri);
	if( method->name == NULL ) {
		free(method);
		return RET_ERROR_OOM;
	}
	method->baseuri = strdup(uri);
	if( method->baseuri == NULL ) {
		free(method->name);
		free(method);
		return RET_ERROR_OOM;
	}
	method->next = run->methods;
	run->methods = method;
	*m = method;
	return RET_OK;
}

/**************************Fire up a method*****************************/

inline static retvalue aptmethod_startup(struct aptmethod *method,const char *methoddir) {
	pid_t f;
	int stdin[2];
	int stdout[2];
	int r;

	if( method->jobs == NULL ) {
		return RET_NOTHING;
	}

	method->status = ams_waitforcapabilities;

	r = pipe(stdin);	
	if( r < 0 ) {
		int err = errno;
		fprintf(stderr,"Error while creating pipe: %d=%m\n",err);
		return RET_ERRNO(err);
	}
	r = pipe(stdout);	
	if( r < 0 ) {
		int err = errno;
		close(stdin[0]);close(stdin[1]);
		fprintf(stderr,"Error while pipe: %d=%m\n",err);
		return RET_ERRNO(err);
	}

	f = fork();
	if( f < 0 ) {
		int err = errno;
		close(stdin[0]);close(stdin[1]);
		close(stdout[0]);close(stdout[1]);
		fprintf(stderr,"Error while forking: %d=%m\n",err);
		return RET_ERRNO(err);
	}
	if( f == 0 ) {
		char *methodname;
		/* child: */
		close(stdin[1]);
		close(stdout[0]);
		if( dup2(stdin[0],0) < 0 ) {
			fprintf(stderr,"Error while setting stdin: %d=%m\n",errno);
			exit(255);
		}
		if( dup2(stdout[1],1) < 0 ) {
			fprintf(stderr,"Error while setting stdin: %d=%m\n",errno);
			exit(255);
		}
		// TODO: close all fd but 0,1,2
	
		methodname = calc_dirconcat(methoddir,method->name);

		if( methodname == NULL )
			exit(255);

		execl(methodname,methodname,NULL);
		
		fprintf(stderr,"Error while executing '%s': %d=%m\n",methodname,errno);
		exit(255);
	}
	/* the main program continues... */
	method->child = f;
	close(stdin[0]);
	close(stdout[1]);
	method->stdin = stdin[1];
	method->stdout = stdout[0];
	method->inputbuffer = NULL;
	method->len = 0;
	method->read = 0;
	return RET_OK;
}

/************************Sending Configuration**************************/

static inline retvalue sendconfig(struct aptmethod *method) {
	struct queuedjob *job;

	fprintf(stderr,"prepare to send config\n");

	job = malloc(sizeof(struct queuedjob));

	job->alreadywritten = 0;
	job->command = mprintf("601 Configuration\nConfig-Item: Dir=/\n\n");
	if( job->command == NULL ) {
		free(job);
		return RET_ERROR_OOM;
	}
	job->len = strlen(job->command);

	job->next = method->jobs;
	method->jobs = job;
	return RET_OK;
}

/**************************how to add files*****************************/

static inline struct tobedone *newtodo(const char *baseuri,const char *origfile,const char *destfile,const char *md5sum) {
	struct tobedone *todo;

	todo = malloc(sizeof(struct tobedone));
	if( todo == NULL )
		return NULL;

	todo->next = NULL;
	todo->uri = calc_dirconcat(baseuri,origfile);
	todo->filename = strdup(destfile);
	if( md5sum ) {
		todo->md5sum = strdup(md5sum);
	} else
		todo->md5sum = NULL;
	if( todo->uri == NULL || todo->filename == NULL ||
			(md5sum != NULL && todo->md5sum == NULL) ) {
		free(todo->uri);
		free(todo->filename);
		free(todo);
		return NULL;
	}
	return todo;
}

retvalue aptmethod_queuefile(struct aptmethod *method,const char *origfile,const char *destfile,const char *md5sum) {
	struct tobedone *todo;
	struct queuedjob *job;

	job = malloc(sizeof(struct queuedjob));
	todo = newtodo(method->baseuri,origfile,destfile,md5sum);
	if( job == NULL || todo == NULL ) {
		free(job);
		return RET_ERROR_OOM;
	}

	job->next = NULL;
	job->alreadywritten = 0;
	job->command = mprintf("600 URI Acquire\nURI: %s\nFilename: %s\n\n",todo->uri,todo->filename);
	if( job->command == NULL ) {
		free(job);
		free(todo->uri);
		free(todo->filename);
		free(todo);
		return RET_ERROR_OOM;
	}
	job->len = strlen(job->command);

	if( method->lasttobedone == NULL )
		method->lasttobedone = method->tobedone = todo;
	else {
		method->lasttobedone->next = todo;
		method->lasttobedone = todo;
	}
	if( method->lastqueued == NULL )
		method->lastqueued = method->jobs = job;
	else {
		method->lastqueued->next = job;
		method->lastqueued = job;
	}
	return RET_OK;
	
}

/*****************what to do with received files************************/

/* process a received file, possibly copying it around... */
static inline retvalue todo_done(struct aptmethod *method,const struct tobedone *todo,const char *filename,const char *md5sum) {
	char *calculatedmd5;

	/* if the file is somewhere else, copy it: */
	if( strcmp(filename,todo->filename) != 0 ) {
		retvalue r;
		if( verbose > 0 ) {
			fprintf(stderr,"Coyping file '%s' to '%s'...\n",filename,todo->filename);
		}

		r = md5sum_copy(filename,todo->filename,&calculatedmd5);
		if( r == RET_NOTHING ) {
			fprintf(stderr,"Cannot open '%s', which was given by method.\n",filename);
			r = RET_ERROR_MISSING;
		}
		if( RET_WAS_ERROR(r) )
			return r;
		/* this we trust more */
		// todo: reimplement the pure copyfile without md5sum?
		md5sum = calculatedmd5;
	} else {
		retvalue r;
	/* if it should be in place, calculate its md5sum, if needed */
		if( todo->md5sum && !md5sum ) {
			r = md5sum_read(filename,&calculatedmd5);
			if( r == RET_NOTHING ) {
				fprintf(stderr,"Cannot open '%s', which was given by method.\n",filename);
				r = RET_ERROR_MISSING;
			}
			if( RET_WAS_ERROR(r) )
				return r;
			md5sum = calculatedmd5;
		} else
			calculatedmd5 = NULL;
	}
	
	/* if we know what it should be, check it: */
	if( todo->md5sum ) {
		if( md5sum == NULL || strcmp(md5sum,todo->md5sum) != 0) {
			fprintf(stderr,"Receiving '%s' wrong md5sum: got '%s' expected '%s'!\n",todo->uri,md5sum,todo->md5sum);
			return RET_ERROR_WRONG_MD5;
		}
	}

	//TODO: call some function here to tell the file is here?
	  
	return RET_OK;
}

/* look where a received file has to go to: */
static retvalue uridone(struct aptmethod *method,const char *uri,const char *filename, const char *md5sum) {
	struct tobedone *todo,*lasttodo;

	lasttodo = NULL; todo = method->tobedone;
	while( todo ) {
		if( strcmp(todo->uri,uri) == 0)  {
			retvalue r;
			r = todo_done(method,todo,filename,md5sum);

			/* remove item: */
			if( lasttodo == NULL )
				method->tobedone = todo->next;
			else
				lasttodo->next = todo->next;
			free(todo->uri);
			free(todo->filename);
			free(todo->md5sum);
			free(todo);
			return r;
		}
		lasttodo = todo;
		todo = todo->next;
	}
	/* huh? */
	fprintf(stderr,"Received unexpected file '%s' at '%s'!",uri,filename);
	return RET_ERROR;
}

/***************************Input and Output****************************/
static inline retvalue gotcapabilities(struct aptmethod *method,const char *chunk) {
	retvalue r;

	r = chunk_gettruth(chunk,"Single-Instance");
	if( RET_WAS_ERROR(r) )
		return r;
	if( r != RET_NOTHING ) {
		fprintf(stderr,"WARNING: Single-Instance not yet supported!\n");
	}
	r = chunk_gettruth(chunk,"Send-Config");
	if( RET_WAS_ERROR(r) )
		return r;
	if( r != RET_NOTHING ) {
		r = sendconfig(method);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	method->status = ams_ok;
	return RET_OK;
}

static inline retvalue goturidone(struct aptmethod *method,const char *chunk) {
	retvalue r;
	char *uri,*filename,*md5,*size,*md5sum;

	//TODO: is it worth the mess to make this in-situ?
	
	r = chunk_getvalue(chunk,"URI",&uri);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing URI-header in uridone got from method!\n");
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;

	r = chunk_getvalue(chunk,"Filename",&filename);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Missing Filename-header in uridone got from method!\n");
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(uri);
		return r;
	}
	
	md5sum = NULL;
	r = chunk_getvalue(chunk,"MD5-Hash",&md5);
	if( RET_IS_OK(r) ) {
		r = chunk_getvalue(chunk,"Size",&size);
		if( RET_IS_OK(r) ) {
			md5sum = calc_concatmd5andsize(md5,size);
			if( md5sum == NULL )
				r = RET_ERROR_OOM;
			free(size);
		}
		free(md5);
	}
	if( RET_WAS_ERROR(r) ) {
		free(uri);
		free(filename);
		return r;
	}

 	r = uridone(method,uri,filename,md5sum);
	free(uri);
	free(filename);
	free(md5sum);
	return r;
}
	
static inline retvalue parsereceivedblock(struct aptmethod *method,const char *input) {
	const char *p;
#define OVERLINE {while( *p && *p != '\n') p++; if(*p == '\n') p++; }

	while( *input == '\n' || *input == '\r' )
		input++;
	if( *input == '\0' ) {
		fprintf(stderr,"Unexpected many newlines from methdod!\n");
		return RET_NOTHING;
	}
	p = input;
	switch( (*(input+1)=='0')?*input:'\0' ) {
		case '1':
			switch( *(input+2) ) {
				/* 100 Capabilities */
				case '0':
					fprintf(stderr,"Got '%s'\n",input);
					OVERLINE;
					return gotcapabilities(method,p);
				/* 101 Log */
				case '1':
					fprintf(stderr,"Got '%s'\n",input);
					return RET_OK;
				/* 102 Status */
				case '2':
					fprintf(stderr,"Got '%s'\n",input);
					return RET_OK;
			}
		case '2':
			switch( *(input+2) ) {
				/* 200 URI Start */
				case '0':
					fprintf(stderr,"Got '%s'\n",input);
					return RET_OK;
				/* 201 URI Done */
				case '1':
					fprintf(stderr,"Got '%s'\n",input);
					OVERLINE;
					return goturidone(method,p);
			}

		case '4':
			fprintf(stderr,"Got error or unsupported mesage: '%s'\n",input);
			return RET_ERROR;
		default:
			fprintf(stderr,"unexpected data from method: '%s'\n",input);
			return RET_ERROR;
	}
}

static retvalue receivedata(struct aptmethod *method) {
	retvalue result;
	size_t r;
	char *p;
	int consecutivenewlines;

	/* First look if we have enough room to read.. */
	if( method->read + 1024 >= method->len ) {
		char *newptr;
		
		if( method->len >= 128000 ) {
			fprintf(stderr,"Ridiculous long answer from method!\n");
			method->status = ams_failed;
			return RET_ERROR;
		}
		
		newptr = realloc(method->inputbuffer,method->read+1024);
		if( newptr == NULL ) {
			return RET_ERROR_OOM;
		}
		method->inputbuffer = newptr;
		method->len = method->read + 1024;
	}
	/* then read as much as the pipe is able to fill of our buffer */

	r = read(method->stdout,method->inputbuffer+method->read,method->len-method->read-1);

	if( r < 0 ) {
		int err = errno;
		fprintf(stderr,"Error reading pipe from aptmethod: %d=%m\n",err);
		method->status = ams_failed;
		return RET_ERRNO(err);
	}
	method->read += r;

	result = RET_NOTHING;
	while(1) {
		retvalue res;

		r = method->read; 
		p = method->inputbuffer;
		consecutivenewlines = 0;

		while( r > 0 ) {
			if( *p == '\0' ) {
				fprintf(stderr,"Zeros in output from method!\n");
				method->status = ams_failed;
				return RET_ERROR;
			} else if( *p == '\n' ) {
				consecutivenewlines++;
				if( consecutivenewlines >= 2 )
					break;
			} else if( *p != '\r' ) {
				consecutivenewlines = 0;
			}
			p++; r--;
		}
		if( r <= 0 )
			return result;
		*p ='\0'; p++; r--;
		res = parsereceivedblock(method,method->inputbuffer);
		if( r > 0 )
			memmove(method->inputbuffer,p,r);
		method->read = r;
		RET_UPDATE(result,res);
	}
}

static retvalue senddata(struct aptmethod *method) {

	if( method->status != ams_ok )
		return RET_NOTHING;

	while( method->jobs ) {
		struct queuedjob *job = method->jobs;
		size_t r,l;

		l = job->len-job->alreadywritten;

		r = write(method->stdin,job->command+job->alreadywritten,l);
		if( r < 0 ) {
			int err;

			err = errno;
			fprintf(stderr,"Error writing to pipe: %d=%m\n",err);
			//TODO: disable the whole method??
			method->status = ams_failed;
			return RET_ERRNO(err);
		}
		if( r < l ) {
			job->alreadywritten += r;
			fprintf(stderr,"Written %d of %d bytes\n",r,job->len);
			break;
		}

		method->jobs = job->next;
		free(job->command);
		free(job);
	}
	return RET_OK;
}

static retvalue checkchilds(struct aptmethodrun *run) {
	pid_t child;int status; 
	retvalue result = RET_OK;

	while( (child = waitpid(-1,&status,WNOHANG)) > 0 ) {
		struct aptmethod *method,*lastmethod;

		lastmethod = NULL; method = run->methods;
		while( method ) {
			if( method->child == child )
				break;
			lastmethod = method;
			method = method->next;
		}
		if( method == NULL ) {
			fprintf(stderr,"Unexpected child died: %d\n",(int)child);
			continue;
		}
		/* remove this child out of the list: */
		if( lastmethod ) {
			lastmethod->next = method->next;
		} else
			run->methods = method->next;

		/* say something if it exited unnormal: */
		if( WIFEXITED(status) ) {
			int exitcode;

			exitcode = WEXITSTATUS(status);
			if( exitcode != 0 ) {
				fprintf(stderr,"Method %s://%s exited with non-zero exit-code %d!\n",method->name,method->baseuri,exitcode);
				result = RET_ERROR;
			}
		} else {
			fprintf(stderr,"Method %s://%s exited unnormally!\n",method->name,method->baseuri);
				result = RET_ERROR;
		}

		/* free the data... */
		aptmethod_free(method);
	}
	return result;
}

static retvalue readwrite(struct aptmethodrun *run,int *activity) {
	int maxfd,v;
	fd_set readfds,writefds;
	struct aptmethod *method;
	retvalue result,r;

	/* First calculate what to look at: */
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	maxfd = 0;
	*activity = 0;
	for( method = run->methods ; method ; method = method->next ) {
		if( method->status == ams_ok && method->jobs ) {
			FD_SET(method->stdin,&writefds);
			if( method->stdin > maxfd )
				maxfd = method->stdin;
			(*activity)++;
			fprintf(stderr,"want to write to '%s'\n",method->baseuri);
		}
		if( method->status == ams_waitforcapabilities ||
				method->tobedone ) {
			FD_SET(method->stdout,&readfds);
			if( method->stdout > maxfd )
				maxfd = method->stdout;
			(*activity)++;
			fprintf(stderr,"want to read from '%s'\n",method->baseuri);
		}
	}

	if( *activity == 0 )
		return RET_NOTHING;

	// TODO: think about a timeout...
	v = select(maxfd+1,&readfds,&writefds,NULL,NULL);
	if( v < 0 ) {
		int err = errno;
		fprintf(stderr,"Select returned error: %d=%m\n",err);
		*activity = -1;
		// TODO: what to do here?
		return RET_ERRNO(errno);
	}

	result = RET_NOTHING;

	maxfd = 0;
	for( method = run->methods ; method ; method = method->next ) {
		if( FD_ISSET(method->stdout,&readfds) ) {
			r = receivedata(method);
			RET_UPDATE(result,r);
		}
		if( FD_ISSET(method->stdin,&writefds) ) {
			r = senddata(method);
			RET_UPDATE(result,r);
		}
	}
	return result;
}

retvalue aptmethod_download(struct aptmethodrun *run,const char *methoddir) {
	struct aptmethod *method,*lastmethod;
	retvalue result,r;
	int activity;

	result = RET_NOTHING;

	/* fire up all methods, removing those that do not work: */
	lastmethod = NULL; method = run->methods;
	while( method ) {
		r = aptmethod_startup(method,methoddir);
		if( !RET_IS_OK(r) ) {
			struct aptmethod *next = method->next;

			if( lastmethod ) {
				lastmethod->next = next;
			} else
				run->methods = next;

			aptmethod_free(method);
			method = next;
		} else {
			lastmethod = method;
			method = method->next;
		}
		RET_UPDATE(result,r);
	}
	/* waiting for them to finish: */
	do {
	  r = checkchilds(run);
	  RET_UPDATE(result,r);
	  r = readwrite(run,&activity);
	  RET_UPDATE(result,r);
	} while( activity > 0 );

	/* finally get rid of all the processes: */
	for( method = run->methods ; method ; method = method->next ) {
		close(method->stdin);method->stdin = -1;
		close(method->stdout);method->stdout = -1;
	}
	while( run->methods ) {
		pid_t pid;int status;
		
		pid = wait(&status);
		lastmethod = NULL; method = run->methods;
		while( method ) {
			if( method->child == pid ) {
				struct aptmethod *next = method->next;

				if( lastmethod ) {
					lastmethod->next = next;
				} else
					run->methods = next;

				aptmethod_free(method);
				method = next;
			} else {
				lastmethod = method;
				method = method->next;
			}
		}
	}
	return result;
}

