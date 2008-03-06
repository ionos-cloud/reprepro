/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007 Bernhard R. Link
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
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "checksums.h"
#include "files.h"
#include "aptmethod.h"
#include "filecntl.h"

extern int verbose;


struct aptmethod {
	/*@only@*/ /*@null@*/
	struct aptmethod *next;
	char *name;
	char *baseuri;
	/*@null@*/char *fallbackbaseuri;
	/*@null@*/char *config;
	int mstdin,mstdout;
	pid_t child;

	enum { ams_notstarted=0, ams_waitforcapabilities, ams_ok, ams_failed } status;

	/*@null@*/struct tobedone *tobedone;
	/*@null@*//*@dependent@*/struct tobedone *lasttobedone;
	/*@null@*//*@dependent@*/const struct tobedone *nexttosend;
	struct tobedone *failed;
	/* what is currently read: */
	/*@null@*/char *inputbuffer;
	size_t input_size,alreadyread;
	/* What is currently written: */
	/*@null@*/char *command;
	size_t alreadywritten,output_length;
	/* true when we are already trying the fallbacks */
	bool fallenback;
};

struct aptmethodrun {
	struct aptmethod *methods;
};


static void free_todolist(/*@only@*/ struct tobedone *todo) {

	while( todo != NULL ) {
		struct tobedone *h = todo->next;

		free(todo->filekey);
		free(todo->filename);
		if( todo->filekey != NULL )
			checksums_free(todo->checksums);
		free(todo->uri);
		free(todo);
		todo = h;
	}
}

static void aptmethod_free(/*@only@*/struct aptmethod *method) {
	if( method == NULL )
		return;
	free(method->name);
	free(method->baseuri);
	free(method->config);
	free(method->fallbackbaseuri);
	free(method->inputbuffer);
	free(method->command);

	free_todolist(method->tobedone);
	free_todolist(method->failed);

	free(method);
}

retvalue aptmethod_shutdown(struct aptmethodrun *run) {
	struct aptmethod *method,*lastmethod,**method_ptr;

	/* first get rid of everything not running: */
	method_ptr = &run->methods;
	while( *method_ptr != NULL ) {

		if( (*method_ptr)->child > 0 ) {
			if( verbose > 10 )
				fprintf(stderr,"Still waiting for %d\n",(int)(*method_ptr)->child);
			method_ptr = &(*method_ptr)->next;
			continue;
		} else {
			/*@only@*/ struct aptmethod *h;
			h = (*method_ptr);
			*method_ptr = h->next;
			h->next = NULL;
			aptmethod_free(h);
		}
	}

	/* finally get rid of all the processes: */
	for( method = run->methods ; method != NULL ; method = method->next ) {
		if( method->mstdin >= 0 ) {
			(void)close(method->mstdin);
			if( verbose > 30 )
				fprintf(stderr, "Closing stdin of %d\n",
						(int)method->child);
		}
		method->mstdin = -1;
		if( method->mstdout >= 0 ) {
			(void)close(method->mstdout);
			if( verbose > 30 )
				fprintf(stderr, "Closing stdout of %d\n",
						(int)method->child);
		}
		method->mstdout = -1;
	}
	while( run->methods != NULL ) {
		pid_t pid;int status;

		pid = wait(&status);
		lastmethod = NULL; method = run->methods;
		while( method != NULL ) {
			if( method->child == pid ) {
				struct aptmethod *next = method->next;

				if( lastmethod != NULL ) {
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
	free(run);
	return RET_OK;
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

retvalue aptmethod_newmethod(struct aptmethodrun *run, const char *uri, const char *fallbackuri, const struct strlist *config, struct aptmethod **m) {
	struct aptmethod *method;
	const char *p;

	method = calloc(1,sizeof(struct aptmethod));
	if( method == NULL )
		return RET_ERROR_OOM;
	method->mstdin = -1;
	method->mstdout = -1;
	method->child = -1;
	method->status = ams_notstarted;
	p = uri;
	while( *p != '\0' && ( *p == '_' || *p == '-' ||
		(*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
		(*p>='0' && *p<='9') ) ) {
		p++;
	}
	if( *p == '\0' ) {
		fprintf(stderr, "No colon found in method-URI '%s'!\n", uri);
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
	if( fallbackuri == NULL )
		method->fallbackbaseuri = NULL;
	else {
		method->fallbackbaseuri = strdup(fallbackuri);
		if( method->fallbackbaseuri == NULL ) {
			free(method->baseuri);
			free(method->name);
			free(method);
			return RET_ERROR_OOM;
		}
	}
#define CONF601 "601 Configuration"
#define CONFITEM "\nConfig-Item: "
	if( config->count == 0 )
		method->config = strdup(CONF601 CONFITEM "Dir=/" "\n\n");
	else
		method->config = strlist_concat(config,
				CONF601 CONFITEM, CONFITEM, "\n\n");
	if( method->config == NULL ) {
		free(method->fallbackbaseuri);
		free(method->baseuri);
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

inline static retvalue aptmethod_startup(struct aptmethod *method, const char *methoddir) {
	pid_t f;
	int mstdin[2];
	int mstdout[2];
	int r;

	/* new try, new luck */
	method->fallenback = false;

	/* When there is nothing to get, there is no reason to startup
	 * the method. */
	if( method->tobedone == NULL ) {
		return RET_NOTHING;
	}

	/* when we are already running, we are already ready...*/
	if( method->child > 0 ) {
		return RET_OK;
	}

	method->status = ams_waitforcapabilities;

	r = pipe(mstdin);
	if( r < 0 ) {
		int e = errno;
		fprintf(stderr, "Error %d creating pipe: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}
	r = pipe(mstdout);
	if( r < 0 ) {
		int e = errno;
		(void)close(mstdin[0]); (void)close(mstdin[1]);
		fprintf(stderr, "Error %d in pipe syscall: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}

	if( interrupted() ) {
		(void)close(mstdin[0]);(void)close(mstdin[1]);
		(void)close(mstdout[0]);(void)close(mstdout[1]);
		return RET_ERROR_INTERRUPTED;
	}
	f = fork();
	if( f < 0 ) {
		int e = errno;
		(void)close(mstdin[0]); (void)close(mstdin[1]);
		(void)close(mstdout[0]); (void)close(mstdout[1]);
		fprintf(stderr, "Error %d forking: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}
	if( f == 0 ) {
		char *methodname;
		int e;
		/* child: */
		(void)close(mstdin[1]);
		(void)close(mstdout[0]);
		if( dup2(mstdin[0],0) < 0 ) {
			int e = errno;
			fprintf(stderr, "Error %d while setting stdin: %s\n",
					e, strerror(e));
			exit(255);
		}
		if( dup2(mstdout[1],1) < 0 ) {
			int e = errno;
			fprintf(stderr, "Error %d while setting stdout: %s\n",
					e, strerror(e));
			exit(255);
		}
		closefrom(3);

		methodname = calc_dirconcat(methoddir,method->name);

		if( methodname == NULL )
			exit(255);

		(void)execl(methodname,methodname,NULL);

		e = errno;
		fprintf(stderr, "Error %d while executing '%s': %s\n",
				e, methodname, strerror(e));
		exit(255);
	}
	/* the main program continues... */
	method->child = f;
	if( verbose > 10 )
		fprintf(stderr,"Method '%s' started as %d\n",method->baseuri,(int)f);
	(void)close(mstdin[0]);
	(void)close(mstdout[1]);
	markcloseonexec(mstdin[1]);
	markcloseonexec(mstdout[0]);
	method->mstdin = mstdin[1];
	method->mstdout = mstdout[0];
	method->inputbuffer = NULL;
	method->input_size = 0;
	method->alreadyread = 0;
	method->command = NULL;
	method->output_length = 0;
	method->alreadywritten = 0;
	return RET_OK;
}

/**************************how to add files*****************************/

/*@null@*/static inline struct tobedone *newtodo(const char *baseuri, const char *origfile, const char *destfile, /*@null@*/const struct checksums *checksums, /*@null@*/const char *filekey) {
	struct tobedone *todo;

	todo = malloc(sizeof(struct tobedone));
	if( todo == NULL )
		return NULL;

	todo->next = NULL;
	todo->uri = calc_dirconcat(baseuri,origfile);
	todo->filename = strdup(destfile);
	if( filekey != NULL ) {
		assert( checksums != NULL );
		todo->filekey = strdup(filekey);
	} else
		todo->filekey = NULL;
	if( checksums != NULL ) {
		assert( filekey != NULL );
		todo->checksums = checksums_dup(checksums);
	} else
		todo->checksums = NULL;
	if( todo->uri == NULL || todo->filename == NULL ||
			(checksums != NULL && todo->checksums == NULL) ||
			(filekey != NULL && todo->filekey == NULL) ) {
		checksums_free(todo->checksums);
		free(todo->uri);
		free(todo->filename);
		free(todo);
		return NULL;
	}
	return todo;
}

retvalue aptmethod_queuefile(struct aptmethod *method, const char *origfile, const char *destfile, const struct checksums *checksums, const char *filekey, struct tobedone **t) {
	struct tobedone *todo;

	assert( origfile != NULL ); assert( destfile != NULL );
	assert( checksums != NULL ); assert( filekey != NULL );
	assert( t != NULL );

	todo = newtodo(method->baseuri, origfile, destfile, checksums, filekey);
	if( todo == NULL )
		return RET_ERROR_OOM;

	if( method->lasttobedone == NULL )
		method->nexttosend = method->lasttobedone = method->tobedone = todo;
	else {
		method->lasttobedone->next = todo;
		method->lasttobedone = todo;
	}
	*t = todo;
	return RET_OK;
}

retvalue aptmethod_queueindexfile(struct aptmethod *method, const char *origfile, const char *destfile, /*@null@*/struct checksums **checksums_p) {
	struct tobedone *todo;

	if( origfile == NULL || destfile == NULL )
		return RET_ERROR_OOM;

	todo = newtodo(method->baseuri, origfile, destfile, NULL, NULL);
	if( todo == NULL )
		return RET_ERROR_OOM;
	todo->checksums_p = checksums_p;

	if( method->lasttobedone == NULL )
		method->nexttosend = method->lasttobedone = method->tobedone = todo;
	else {
		method->lasttobedone->next = todo;
		method->lasttobedone = todo;
	}
	return RET_OK;
}

/*****************what to do with received files************************/

/* process a received file, possibly copying it around... */
static inline retvalue todo_done(struct tobedone *todo, const char *method, const char *filename, /*@only@*//*@null@*/struct checksums *checksumsfromapt, struct database *database) {
	retvalue r;
	struct checksums *checksums = NULL;
	struct checksums **checksums_p;

	if( todo->filekey != NULL )
		checksums_p = &todo->checksums;
	else
		checksums_p = todo->checksums_p;

	/* if the file is somewhere else, copy it: */
	if( strcmp(filename, todo->filename) != 0 ) {
		/* never link index files, but copy them */
		if( todo->filekey == NULL ) {
			if( verbose > 1 )
				fprintf(stderr,
"Copy file '%s' to '%s'...\n", filename, todo->filename);
			r = checksums_copyfile(todo->filename, filename,
					&checksums);
		} else {
			if( verbose > 1 )
				fprintf(stderr,
"Linking file '%s' to '%s'...\n", filename, todo->filename);
			r = checksums_linkorcopyfile(todo->filename, filename,
					&checksums);
		}
		if( r == RET_NOTHING ) {
			fprintf(stderr, "Cannot open '%s', obtained from '%s' method.\n",
					filename, method);
			r = RET_ERROR_MISSING;
		}
		if( RET_WAS_ERROR(r) ) {
			checksums_free(checksumsfromapt);
			return r;
		}
	}

	if( checksums_p != NULL && checksums == NULL ) {
		/* trust apt-method if it gave some checksums */
		checksums = checksumsfromapt;
		if( checksums == NULL )
			r = checksums_read(filename, &checksums);
		else
			/* but make sure it computed all we would have, too */
			r = checksums_complete(&checksums, filename);
		if( r == RET_NOTHING ) {
			fprintf(stderr, "Cannot open '%s', though '%s' method claims to have put it there!\n",
					filename, method);
			r = RET_ERROR_MISSING;
		}
		if( RET_WAS_ERROR(r) )
			return r;
	} else
		checksums_free(checksumsfromapt);

	/* if we know what it should be, check it: */
	if( checksums_p != NULL ) {
		bool improves;

		assert( checksums != NULL );
		assert( *checksums_p != NULL );

		if( !checksums_check(*checksums_p, checksums, &improves) ) {
			fprintf(stderr, "Wrong checksum during receive of '%s':\n",
					todo->uri);
			checksums_printdifferences(stderr, *checksums_p,
					checksums);
			checksums_free(checksums);
			return RET_ERROR_WRONG_MD5;
		}
		if( improves ) {
			r = checksums_combine(checksums_p, checksums);
			if( RET_WAS_ERROR(r) ) {
				checksums_free(checksums);
				return r;
			}
		}
	}
	checksums_free(checksums);

	if( todo->filekey != NULL ) {
		assert(todo->checksums != NULL);
		r = files_add_checksums(database, todo->filekey, todo->checksums);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	return RET_OK;
}

/* shuffle files from failed to tobedone and nexttosend, if an
 * alternate baseuri to try too is given. */
static retvalue requeue_failed(struct aptmethod *method){
	struct tobedone *todo;
	size_t old_len,new_len;

	if( method->failed == NULL )
		return RET_OK;
	if( method->fallbackbaseuri == NULL || method->fallenback )
		return RET_ERROR;
	method->tobedone = method->failed;
	method->nexttosend = method->failed;
	method->failed = NULL;

	old_len = strlen(method->baseuri);
	new_len = strlen(method->fallbackbaseuri);

	/* try at most once */
	method->fallenback = true;

	for( todo = method->tobedone; todo != NULL ; todo = todo->next ) {
		size_t l;
		char *s;

		l = strlen(todo->uri);
		s = malloc(l+new_len+1-old_len);
		if( s != NULL ) {
			memcpy(s,method->fallbackbaseuri,new_len);
			strcpy(s+new_len,todo->uri + old_len);
			free(todo->uri);
			todo->uri = s;
		}
	}

	return RET_OK;
}

/* look which file could not be received and remove it: */
static retvalue urierror(struct aptmethod *method,const char *uri,/*@only@*/char *message) {
	struct tobedone *todo,*lasttodo;

	lasttodo = NULL; todo = method->tobedone;
	while( todo != NULL ) {
		if( strcmp(todo->uri,uri) == 0)  {

			/* remove item: */
			if( lasttodo == NULL )
				method->tobedone = todo->next;
			else
				lasttodo->next = todo->next;
			if( method->nexttosend == todo ) {
				/* just in case some method received
				 * files before we request them ;-) */
				method->nexttosend = todo->next;
			}
			if( method->lasttobedone == todo ) {
				method->lasttobedone = todo->next;
			}
			fprintf(stderr,"aptmethod error receiving '%s':\n'%s'\n",uri,message);
			/* put message in failed items to show it later? */
			free(message);
			/* put it in failed list to cope with it later */
			todo->next = method->failed;
			method->failed = todo;
			/* if everything has answered, redo failed */
			if( method->tobedone == NULL ) {
				return requeue_failed(method);
			}
			return RET_OK;
		}
		lasttodo = todo;
		todo = todo->next;
	}
	/* huh? If if have not asked for it, how can there be errors? */
	fprintf(stderr, "Method '%s' reported error with unrequested file '%s':\n'%s'!\n",
			method->name, uri, message);
	free(message);
	return RET_ERROR;
}

/* look where a received file has to go to: */
static retvalue uridone(struct aptmethod *method, const char *uri, const char *filename, /*@only@*//*@null@*/struct checksums *checksumsfromapt, struct database *database) {
	struct tobedone *todo,*lasttodo;

	lasttodo = NULL; todo = method->tobedone;
	while( todo != NULL ) {
		if( strcmp(todo->uri,uri) == 0)  {
			retvalue r;
			r = todo_done(todo, method->name, filename,
					checksumsfromapt, database);

			/* remove item: */
			if( lasttodo == NULL )
				method->tobedone = todo->next;
			else
				lasttodo->next = todo->next;
			if( method->nexttosend == todo ) {
				/* just in case some method received
				 * files before we request them ;-) */
				method->nexttosend = todo->next;
			}
			if( method->lasttobedone == todo ) {
				method->lasttobedone = todo->next;
			}
			free(todo->uri);
			free(todo->filename);
			if( todo->filekey != NULL )
				checksums_free(todo->checksums);
			free(todo->filekey);
			free(todo);
			/* if everything is done, redo failed */
			if( method->tobedone == NULL ) {
				retvalue rr;
				rr = requeue_failed(method);
				RET_UPDATE(r,rr);
			}
			return r;
		}
		lasttodo = todo;
		todo = todo->next;
	}
	/* huh? */
	fprintf(stderr, "Method '%s' retrieved unexpected file '%s' at '%s'!\n",
			method->name, uri, filename);
	checksums_free(checksumsfromapt);
	return RET_ERROR;
}

/***************************Input and Output****************************/
static retvalue logmessage(const struct aptmethod *method,const char *chunk,const char *type) {
	retvalue r;
	char *message;

	r = chunk_getvalue(chunk,"Message",&message);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) ) {
		fprintf(stderr,"aptmethod '%s': '%s'\n",method->baseuri,message);
		free(message);
		return RET_OK;
	}
	r = chunk_getvalue(chunk,"URI",&message);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) ) {
		fprintf(stderr,"aptmethod %s '%s'\n",type,message);
		free(message);
		return RET_OK;
	}
	fprintf(stderr,"aptmethod '%s': '%s'\n",method->baseuri,type);
	return RET_OK;
}
static inline retvalue gotcapabilities(struct aptmethod *method,const char *chunk) {
	retvalue r;

	r = chunk_gettruth(chunk,"Single-Instance");
	if( RET_WAS_ERROR(r) )
		return r;
// TODO: what to do with this?
//	if( r != RET_NOTHING ) {
//		fprintf(stderr,"WARNING: Single-instance not yet supported!\n");
//	}
	r = chunk_gettruth(chunk,"Send-Config");
	if( RET_WAS_ERROR(r) )
		return r;
	if( r != RET_NOTHING ) {
		assert(method->command == NULL);
		method->alreadywritten = 0;
		method->command = method->config;
		method->config = NULL;
		method->output_length = strlen(method->command);
		if( verbose > 11 ) {
			fprintf(stderr,"Sending config: '%s'\n",method->command);
		}
	} else {
		free(method->config);
		method->config = NULL;
	}
	method->status = ams_ok;
	return RET_OK;
}

static inline retvalue goturidone(struct aptmethod *method,const char *chunk,struct database *database) {
	static const char * const method_hash_names[cs_COUNT] =
		{ "MD5-Hash", "SHA1-Hash", // "SHA256-Hash"
		  "Size" };
	retvalue result, r;
	char *uri, *filename;
	enum checksumtype type;
	char *hashes[cs_COUNT];
	struct checksums *checksums = NULL;

	//TODO: is it worth the mess to make this in-situ?

	r = chunk_getvalue(chunk,"URI",&uri);
	if( r == RET_NOTHING ) {
		fprintf(stderr,
"Missing URI header in uridone received from '%s' method!\n",
				method->name);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;

	r = chunk_getvalue(chunk,"Filename",&filename);
	if( r == RET_NOTHING ) {
		fprintf(stderr,
"Missing Filename header in uridone received from '%s' method!\n",
				method->name);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) ) {
		free(uri);
		return r;
	}

	result = RET_NOTHING;
	for( type = cs_md5sum ; type < cs_COUNT ; type++ ) {
		hashes[type] = NULL;
		r = chunk_getvalue(chunk, method_hash_names[type],
				&hashes[type]);
		RET_UPDATE(result, r);
	}
	if( RET_IS_OK(result) && hashes[cs_md5sum] == NULL ) {
		/* the lenny version also has this, better ask for
		 * in case the old MD5-Hash vanishes in the future */
		r = chunk_getvalue(chunk, "MD5Sum-Hash", &hashes[cs_md5sum]);
		RET_UPDATE(result, r);
	}
	if( RET_WAS_ERROR(result) ) {
		free(uri); free(filename);
		for( type = cs_md5sum ; type < cs_COUNT ; type++ )
			free(hashes[type]);
		return result;
	}
	if( RET_IS_OK(result) ) {
		/* ignore errors, we can recompute them from the file */
		(void)checksums_init(&checksums, hashes);
	}
 	r = uridone(method, uri, filename, checksums, database);
	free(uri);
	free(filename);
	return r;
}

static inline retvalue goturierror(struct aptmethod *method,const char *chunk) {
	retvalue r;
	char *uri,*message;

	r = chunk_getvalue(chunk,"URI",&uri);
	if( r == RET_NOTHING ) {
		fprintf(stderr,
"Missing URI header in urierror received from '%s' method!\n", method->name);
		r = RET_ERROR;
	}
	if( RET_WAS_ERROR(r) )
		return r;

	r = chunk_getvalue(chunk,"Message",&message);
	if( r == RET_NOTHING ) {
		message = NULL;
	}
	if( RET_WAS_ERROR(r) ) {
		free(uri);
		return r;
	}

 	r = urierror(method,uri,message);
	free(uri);
	return r;
}

static inline retvalue parsereceivedblock(struct aptmethod *method,const char *input,struct database *database) {
	const char *p;
	retvalue r;
#define OVERLINE {while( *p != '\0' && *p != '\n') p++; if(*p == '\n') p++; }

	while( *input == '\n' || *input == '\r' )
		input++;
	if( *input == '\0' ) {
		fprintf(stderr,
"Unexpected number of newlines from '%s' method!\n", method->name);
		return RET_NOTHING;
	}
	p = input;
	switch( (*(input+1)=='0')?*input:'\0' ) {
		case '1':
			switch( *(input+2) ) {
				/* 100 Capabilities */
				case '0':
					OVERLINE;
					if( verbose > 14 ) {
						fprintf(stderr,"Got '%s'\n",input);
					}
					return gotcapabilities(method,input);
				/* 101 Log */
				case '1':
					if( verbose > 10 ) {
						OVERLINE;
						return logmessage(method,p,"101");
					}
					return RET_OK;
				/* 102 Status */
				case '2':
					if( verbose > 5 ) {
						OVERLINE;
						return logmessage(method,p,"102");
					}
					return RET_OK;
				default:
					fprintf(stderr,
"Error or unsupported message received: '%s'\n",	input);
					return RET_ERROR;
			}
		case '2':
			switch( *(input+2) ) {
				/* 200 URI Start */
				case '0':
					if( verbose > 5 ) {
						OVERLINE;
						return logmessage(method,p,"start");
					}
					return RET_OK;
				/* 201 URI Done */
				case '1':
					OVERLINE;
					if( verbose >= 1 )
						logmessage(method,p,"got");
					return goturidone(method, p, database);
				default:
					fprintf(stderr,
"Error or unsupported message received: '%s'\n",	input);
					return RET_ERROR;
			}

		case '4':
			switch( *(input+2) ) {
				case '0':
					OVERLINE;
					r = goturierror(method,p);
					break;
				case '1':
					OVERLINE;
					(void)logmessage(method,p,"general error");
					method->status = ams_failed;
					r = RET_ERROR;
					break;
				default:
					fprintf(stderr,
"Error or unsupported message received: '%s'\n",	input);
					r = RET_ERROR;
			}
			/* a failed download is not a error yet, as it might
			 * be redone from another source later */
			return r;
		default:
			fprintf(stderr, "Unexpected data from '%s' method: '%s'\n",
					method->name, input);
			return RET_ERROR;
	}
}

static retvalue receivedata(struct aptmethod *method,struct database *database) {
	retvalue result;
	ssize_t r;
	char *p;
	int consecutivenewlines;

	assert( method->status != ams_ok || method->tobedone != NULL );
	if( method->status != ams_waitforcapabilities && method->status != ams_ok )
		return RET_NOTHING;

	/* First look if we have enough room to read.. */
	if( method->alreadyread + 1024 >= method->input_size ) {
		char *newptr;

		if( method->input_size >= (size_t)128000 ) {
			fprintf(stderr, "Ridiculously long answer from method!\n");
			method->status = ams_failed;
			return RET_ERROR;
		}

		newptr = realloc(method->inputbuffer,method->alreadyread+1024);
		if( newptr == NULL ) {
			return RET_ERROR_OOM;
		}
		method->inputbuffer = newptr;
		method->input_size = method->alreadyread + 1024;
	}
	assert( method->inputbuffer != NULL );
	/* then read as much as the pipe is able to fill of our buffer */

	r = read(method->mstdout,method->inputbuffer+method->alreadyread,method->input_size-method->alreadyread-1);

	if( r < 0 ) {
		int e = errno;
		fprintf(stderr, "Error %d reading pipe from aptmethod: %s\n",
				e, strerror(e));
		method->status = ams_failed;
		return RET_ERRNO(e);
	}
	method->alreadyread += r;

	result = RET_NOTHING;
	while(true) {
		retvalue res;

		r = method->alreadyread;
		p = method->inputbuffer;
		consecutivenewlines = 0;

		while( r > 0 ) {
			if( *p == '\0' ) {
				fprintf(stderr, "Unexpected Zeroes in method output!\n");
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
		if( r <= 0 ) {
			return result;
		}
		*p ='\0'; p++; r--;
		res = parsereceivedblock(method, method->inputbuffer, database);
		if( r > 0 )
			memmove(method->inputbuffer,p,r);
		method->alreadyread = r;
		RET_UPDATE(result,res);
	}
}

static retvalue senddata(struct aptmethod *method) {
	size_t l;
	ssize_t r;

	if( method->status != ams_ok )
		return RET_NOTHING;

	if( method->command == NULL ) {
		/* nothing queued to send, nothing to be queued...*/
		if( method->nexttosend == NULL ) {
			return RET_OK;
		}

		if( interrupted() )
			return RET_ERROR_INTERRUPTED;

		method->alreadywritten = 0;
		// TODO: make sure this is already checked for earlier...
		assert(strchr(method->nexttosend->uri,'\n')==NULL || strchr(method->nexttosend->filename,'\n') == 0);
		/* http-aptmethod seems to loose the last byte if the file is already
		 * in place, so we better unlink the target first... */
		unlink(method->nexttosend->filename);
		method->command = mprintf(
			 "600 URI Acquire\nURI: %s\nFilename: %s\n\n",
			 method->nexttosend->uri,method->nexttosend->filename);
		if( method->command == NULL ) {
			return RET_ERROR_OOM;
		}
		method->output_length = strlen(method->command);
		method->nexttosend = method->nexttosend->next;
	}


	l = method->output_length - method->alreadywritten;

	r = write(method->mstdin,method->command+method->alreadywritten,l);
	if( r < 0 ) {
		int e = errno;

		fprintf(stderr, "Error %d writing to pipe: %s\n",
				e, strerror(e));
		//TODO: disable the whole method??
		method->status = ams_failed;
		return RET_ERRNO(e);
	} else if( (size_t)r < l ) {
		method->alreadywritten += r;
		return RET_OK;
	}

	free(method->command);
	method->command = NULL;
	return RET_OK;
}

static retvalue checkchilds(struct aptmethodrun *run) {
	pid_t child;int status;
	retvalue result = RET_OK;

	while( (child = waitpid(-1,&status,WNOHANG)) > 0 ) {
		struct aptmethod *method;

		for( method = run->methods; method != NULL ; method = method->next) {
			if( method->child == child )
				break;
		}
		if( method == NULL ) {
			fprintf(stderr,
"Unexpected child died (maybe gpg died if signing/verifing was done): %d\n",
					(int)child);
			continue;
		}
		/* Make sure we do not cope with this child any more */
		if( method->mstdin != -1 ) {
			(void)close(method->mstdin);
			method->mstdin = -1;
		}
		if( method->mstdout != -1 ) {
			(void)close(method->mstdout);
			method->mstdout = -1;
		}
		method->child = -1;
		if( method->status != ams_failed )
			method->status = ams_notstarted;

		/* say something if it exited unnormal: */
		if( WIFEXITED(status) ) {
			int exitcode;

			exitcode = WEXITSTATUS(status);
			if( exitcode != 0 ) {
				fprintf(stderr,
"Method %s://%s exited with non-zero exit code %d!\n",
					method->name, method->baseuri,
					exitcode);
				method->status = ams_notstarted;
				result = RET_ERROR;
			}
		} else {
			fprintf(stderr,"Method %s://%s exited unnormally!\n",method->name,method->baseuri);
			method->status = ams_notstarted;
			result = RET_ERROR;
		}
	}
	return result;
}

/* *workleft is always set, even when return indicated error. (workleft < 0 when critical)*/
static retvalue readwrite(struct aptmethodrun *run,/*@out@*/int *workleft,struct database *database) {
	int maxfd,v;
	fd_set readfds,writefds;
	struct aptmethod *method;
	retvalue result,r;

	/* First calculate what to look at: */
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	maxfd = 0;
	*workleft = 0;
	for( method = run->methods ; method != NULL ; method = method->next ) {
		if( method->status == ams_ok &&
		    ( method->command != NULL || method->nexttosend != NULL )) {
			FD_SET(method->mstdin,&writefds);
			if( method->mstdin > maxfd )
				maxfd = method->mstdin;
			(*workleft)++;
			if( verbose > 19 )
				fprintf(stderr,"want to write to '%s'\n",method->baseuri);
		}
		if( method->status == ams_waitforcapabilities ||
				(method->status == ams_ok &&
				method->tobedone != NULL ) ) {
			FD_SET(method->mstdout,&readfds);
			if( method->mstdout > maxfd )
				maxfd = method->mstdout;
			(*workleft)++;
			if( verbose > 19 )
				fprintf(stderr,"want to read from '%s'\n",method->baseuri);
		}
	}

	if( *workleft == 0 )
		return RET_NOTHING;

	// TODO: think about a timeout...
	v = select(maxfd+1,&readfds,&writefds,NULL,NULL);
	if( v < 0 ) {
		int e = errno;
		//TODO: handle (e == EINTR) && interrupted() specially
		fprintf(stderr, "Select returned error %d: %s\n",
				e, strerror(e));
		*workleft = -1;
		// TODO: what to do here?
		return RET_ERRNO(e);
	}

	result = RET_NOTHING;

	maxfd = 0;
	for( method = run->methods ; method != NULL ; method = method->next ) {
		if( method->mstdout != -1 && FD_ISSET(method->mstdout,&readfds) ) {
			r = receivedata(method, database);
			RET_UPDATE(result,r);
		}
		if( method->mstdin != -1 && FD_ISSET(method->mstdin,&writefds) ) {
			r = senddata(method);
			RET_UPDATE(result,r);
		}
	}
	return result;
}

retvalue aptmethod_download(struct aptmethodrun *run,const char *methoddir,struct database *database) {
	struct aptmethod *method;
	retvalue result,r;
	int workleft;

	result = RET_NOTHING;

	/* fire up all methods, removing those that do not work: */
	for( method = run->methods; method != NULL ; method = method->next ) {
		r = aptmethod_startup(method, methoddir);
		/* do not remove failed methods here any longer,
		 * and not remove methods having nothing to do,
		 * as this breaks when no index files are downloaded
		 * due to all already being in place... */
		RET_UPDATE(result,r);
	}
	/* waiting for them to finish: */
	do {
	  r = checkchilds(run);
	  RET_UPDATE(result,r);
	  r = readwrite(run, &workleft, database);
	  RET_UPDATE(result,r);
	  // TODO: check interrupted here...
	} while( workleft > 0 );

	return result;
}

