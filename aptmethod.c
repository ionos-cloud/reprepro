/*  This file is part of "reprepro"
 *  Copyright (C) 2004,2005,2007,2008,2009,2012 Bernhard R. Link
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
#include "uncompression.h"
#include "aptmethod.h"
#include "filecntl.h"
#include "hooks.h"

struct tobedone {
	/*@null@*/
	struct tobedone *next;
	/* must be saved to know where is should be moved to: */
	/*@notnull@*/
	char *uri;
	/*@notnull@*/
	char *filename;
	/* in case of redirection, store the originally requested uri: */
	/*@null@*/
	char *original_uri;
	/* callback and its data: */
	queue_callback *callback;
	/*@null@*/void *privdata1, *privdata2;
	/* there is no fallback or that was already used */
	bool lasttry;
	/* how often this was redirected */
	unsigned int redirect_count;
};

struct aptmethod {
	/*@only@*/ /*@null@*/
	struct aptmethod *next;
	char *name;
	char *baseuri;
	/*@null@*/char *fallbackbaseuri;
	/*@null@*/char *config;
	int mstdin, mstdout;
	pid_t child;

	enum {
		ams_notstarted=0,
		ams_waitforcapabilities,
		ams_ok,
		ams_failed
	} status;

	/*@null@*/struct tobedone *tobedone;
	/*@null@*//*@dependent@*/struct tobedone *lasttobedone;
	/*@null@*//*@dependent@*/const struct tobedone *nexttosend;
	/* what is currently read: */
	/*@null@*/char *inputbuffer;
	size_t input_size, alreadyread;
	/* What is currently written: */
	/*@null@*/char *command;
	size_t alreadywritten, output_length;
};

struct aptmethodrun {
	struct aptmethod *methods;
};

static void todo_free(/*@only@*/ struct tobedone *todo) {
	free(todo->filename);
	free(todo->original_uri);
	free(todo->uri);
	free(todo);
}

static void free_todolist(/*@only@*/ struct tobedone *todo) {

	while (todo != NULL) {
		struct tobedone *h = todo->next;

		todo_free(todo);
		todo = h;
	}
}

static void aptmethod_free(/*@only@*/struct aptmethod *method) {
	if (method == NULL)
		return;
	free(method->name);
	free(method->baseuri);
	free(method->config);
	free(method->fallbackbaseuri);
	free(method->inputbuffer);
	free(method->command);

	free_todolist(method->tobedone);

	free(method);
}

retvalue aptmethod_shutdown(struct aptmethodrun *run) {
	retvalue result = RET_OK, r;
	struct aptmethod *method, *lastmethod, **method_ptr;

	/* first get rid of everything not running: */
	method_ptr = &run->methods;
	while (*method_ptr != NULL) {

		if ((*method_ptr)->child > 0) {
			if (verbose > 10)
				fprintf(stderr,
"Still waiting for %d\n", (int)(*method_ptr)->child);
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
	for (method = run->methods ; method != NULL ; method = method->next) {
		if (method->mstdin >= 0) {
			(void)close(method->mstdin);
			if (verbose > 30)
				fprintf(stderr, "Closing stdin of %d\n",
						(int)method->child);
		}
		method->mstdin = -1;
		if (method->mstdout >= 0) {
			(void)close(method->mstdout);
			if (verbose > 30)
				fprintf(stderr, "Closing stdout of %d\n",
						(int)method->child);
		}
		method->mstdout = -1;
	}
	while (run->methods != NULL || uncompress_running()) {
		pid_t pid;int status;

		pid = wait(&status);
		lastmethod = NULL; method = run->methods;
		while (method != NULL) {
			if (method->child == pid) {
				struct aptmethod *next = method->next;

				if (lastmethod != NULL) {
					lastmethod->next = next;
				} else
					run->methods = next;

				aptmethod_free(method);
				pid = -1;
				break;
			} else {
				lastmethod = method;
				method = method->next;
			}
		}
		if (pid > 0) {
			r = uncompress_checkpid(pid, status);
			RET_UPDATE(result, r);
		}
	}
	free(run);
	return result;
}

/******************Initialize the data structures***********************/

retvalue aptmethod_initialize_run(struct aptmethodrun **run) {
	struct aptmethodrun *r;

	r = zNEW(struct aptmethodrun);
	if (FAILEDTOALLOC(r))
		return RET_ERROR_OOM;
	*run = r;
	return RET_OK;
}

retvalue aptmethod_newmethod(struct aptmethodrun *run, const char *uri, const char *fallbackuri, const struct strlist *config, struct aptmethod **m) {
	struct aptmethod *method;
	const char *p;

	method = zNEW(struct aptmethod);
	if (FAILEDTOALLOC(method))
		return RET_ERROR_OOM;
	method->mstdin = -1;
	method->mstdout = -1;
	method->child = -1;
	method->status = ams_notstarted;
	p = uri;
	while (*p != '\0' && (*p == '_' || *p == '-' || *p == '+' ||
		(*p>='a' && *p<='z') || (*p>='A' && *p<='Z') ||
		(*p>='0' && *p<='9'))) {
		p++;
	}
	if (*p == '\0') {
		fprintf(stderr, "No colon found in method-URI '%s'!\n", uri);
		free(method);
		return RET_ERROR;
	}
	if (*p != ':') {
		fprintf(stderr,
"Unexpected character '%c' in method-URI '%s'!\n", *p, uri);
		free(method);
		return RET_ERROR;
	}
	if (p == uri) {
		fprintf(stderr,
"Zero-length name in method-URI '%s'!\n", uri);
		free(method);
		return RET_ERROR;
	}

	method->name = strndup(uri, p-uri);
	if (FAILEDTOALLOC(method->name)) {
		free(method);
		return RET_ERROR_OOM;
	}
	method->baseuri = strdup(uri);
	if (FAILEDTOALLOC(method->baseuri)) {
		free(method->name);
		free(method);
		return RET_ERROR_OOM;
	}
	if (fallbackuri == NULL)
		method->fallbackbaseuri = NULL;
	else {
		method->fallbackbaseuri = strdup(fallbackuri);
		if (FAILEDTOALLOC(method->fallbackbaseuri)) {
			free(method->baseuri);
			free(method->name);
			free(method);
			return RET_ERROR_OOM;
		}
	}
#define CONF601 "601 Configuration"
#define CONFITEM "\nConfig-Item: "
	if (config->count == 0)
		method->config = strdup(CONF601 CONFITEM "Dir=/" "\n\n");
	else
		method->config = strlist_concat(config,
				CONF601 CONFITEM, CONFITEM, "\n\n");
	if (FAILEDTOALLOC(method->config)) {
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

inline static retvalue aptmethod_startup(struct aptmethod *method) {
	pid_t f;
	int mstdin[2];
	int mstdout[2];
	int r;

	/* When there is nothing to get, there is no reason to startup
	 * the method. */
	if (method->tobedone == NULL) {
		return RET_NOTHING;
	}

	/* when we are already running, we are already ready...*/
	if (method->child > 0) {
		return RET_OK;
	}

	method->status = ams_waitforcapabilities;

	r = pipe(mstdin);
	if (r < 0) {
		int e = errno;
		fprintf(stderr, "Error %d creating pipe: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}
	r = pipe(mstdout);
	if (r < 0) {
		int e = errno;
		(void)close(mstdin[0]); (void)close(mstdin[1]);
		fprintf(stderr, "Error %d in pipe syscall: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}

	if (interrupted()) {
		(void)close(mstdin[0]);(void)close(mstdin[1]);
		(void)close(mstdout[0]);(void)close(mstdout[1]);
		return RET_ERROR_INTERRUPTED;
	}
	f = fork();
	if (f < 0) {
		int e = errno;
		(void)close(mstdin[0]); (void)close(mstdin[1]);
		(void)close(mstdout[0]); (void)close(mstdout[1]);
		fprintf(stderr, "Error %d forking: %s\n",
				e, strerror(e));
		return RET_ERRNO(e);
	}
	if (f == 0) {
		char *methodname;
		int e;
		/* child: */
		(void)close(mstdin[1]);
		(void)close(mstdout[0]);
		if (dup2(mstdin[0], 0) < 0) {
			e = errno;
			fprintf(stderr, "Error %d while setting stdin: %s\n",
					e, strerror(e));
			exit(255);
		}
		if (dup2(mstdout[1], 1) < 0) {
			e = errno;
			fprintf(stderr, "Error %d while setting stdout: %s\n",
					e, strerror(e));
			exit(255);
		}
		closefrom(3);

		methodname = calc_dirconcat(global.methoddir, method->name);
		if (FAILEDTOALLOC(methodname))
			exit(255);

		/* not really useful here, unless someone write reprepro
		 * specific modules (which I hope no one will) */
		sethookenvironment(NULL, NULL, NULL, NULL);
		/* actually call the method without any arguments: */
		(void)execl(methodname, methodname, ENDOFARGUMENTS);

		e = errno;
		fprintf(stderr, "Error %d while executing '%s': %s\n",
				e, methodname, strerror(e));
		exit(255);
	}
	/* the main program continues... */
	method->child = f;
	if (verbose > 10)
		fprintf(stderr,
"Method '%s' started as %d\n", method->baseuri, (int)f);
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

static inline void enqueue(struct aptmethod *method, /*@only@*/struct tobedone *todo) {
	todo->next = NULL;
	if (method->lasttobedone == NULL)
		method->nexttosend = method->lasttobedone = method->tobedone = todo;
	else {
		method->lasttobedone->next = todo;
		method->lasttobedone = todo;
		if (method->nexttosend == NULL)
			method->nexttosend = todo;
	}
}

static retvalue enqueuenew(struct aptmethod *method, /*@only@*/char *uri, /*@only@*/char *destfile, queue_callback *callback, void *privdata1, void *privdata2) {
	struct tobedone *todo;

	if (FAILEDTOALLOC(destfile)) {
		free(uri);
		return RET_ERROR_OOM;
	}
	if (FAILEDTOALLOC(uri)) {
		free(destfile);
		return RET_ERROR_OOM;
	}

	todo = NEW(struct tobedone);
	if (FAILEDTOALLOC(todo)) {
		free(uri); free(destfile);
		return RET_ERROR_OOM;
	}

	todo->next = NULL;
	todo->uri = uri;
	todo->filename = destfile;
	todo->original_uri = NULL;
	todo->callback = callback;
	todo->privdata1 = privdata1;
	todo->privdata2 = privdata2;
	todo->lasttry = method->fallbackbaseuri == NULL;
	todo->redirect_count = 0;
	enqueue(method, todo);
	return RET_OK;
}

retvalue aptmethod_enqueue(struct aptmethod *method, const char *origfile, /*@only@*/char *destfile, queue_callback *callback, void *privdata1, void *privdata2) {
	return enqueuenew(method,
			calc_dirconcat(method->baseuri, origfile),
			destfile, callback, privdata1, privdata2);
}

retvalue aptmethod_enqueueindex(struct aptmethod *method, const char *suite, const char *origfile, const char *suffix, const char *destfile, const char *downloadsuffix, queue_callback *callback, void *privdata1, void *privdata2) {
	return enqueuenew(method,
			mprintf("%s/%s/%s%s",
				method->baseuri, suite, origfile, suffix),
			mprintf("%s%s", destfile, downloadsuffix),
			callback, privdata1, privdata2);
}

/*****************what to do with received files************************/

static retvalue requeue_or_fail(struct aptmethod *method, /*@only@*/struct tobedone *todo) {
	retvalue r;

	if (todo->lasttry) {
		if (todo->callback == NULL)
			r = RET_ERROR;
		else
			r = todo->callback(qa_error,
				todo->privdata1, todo->privdata2,
				todo->uri, NULL, todo->filename,
				NULL, method->name);
		todo_free(todo);
		return r;
	} else {
		size_t l, old_len, new_len;
		char *s;

		assert (method->fallbackbaseuri != NULL);

		old_len = strlen(method->baseuri);
		new_len = strlen(method->fallbackbaseuri);
		l = strlen(todo->uri);
		s = malloc(l+new_len+1-old_len);
		if (FAILEDTOALLOC(s)) {
			todo_free(todo);
			return RET_ERROR_OOM;
		}
		memcpy(s, method->fallbackbaseuri, new_len);
		strcpy(s+new_len, todo->uri + old_len);
		free(todo->uri);
		todo->uri = s;
		todo->lasttry = true;
		todo->redirect_count = 0;
		enqueue(method, todo);
		return RET_OK;
	}
}

/* look which file could not be received and remove it: */
static retvalue urierror(struct aptmethod *method, const char *uri, /*@only@*/char *message) {
	struct tobedone *todo, *lasttodo;

	lasttodo = NULL; todo = method->tobedone;
	while (todo != NULL) {
		if (strcmp(todo->uri, uri) == 0)  {

			/* remove item: */
			if (lasttodo == NULL)
				method->tobedone = todo->next;
			else
				lasttodo->next = todo->next;
			if (method->nexttosend == todo) {
				/* just in case some method received
				 * files before we request them ;-) */
				method->nexttosend = todo->next;
			}
			if (method->lasttobedone == todo) {
				method->lasttobedone = todo->next;
			}
			fprintf(stderr,
"aptmethod error receiving '%s':\n'%s'\n",
					uri, (message != NULL)?message:"");
			/* put message in failed items to show it later? */
			free(message);
			return requeue_or_fail(method, todo);
		}
		lasttodo = todo;
		todo = todo->next;
	}
	/* huh? If if have not asked for it, how can there be errors? */
	fprintf(stderr,
"Method '%s' reported error with unrequested file '%s':\n'%s'!\n",
			method->name, uri, message);
	free(message);
	return RET_ERROR;
}

/* look which file could not be received and readd the new name... */
static retvalue uriredirect(struct aptmethod *method, const char *uri, /*@only@*/char *newuri) {
	struct tobedone *todo, *lasttodo;

	lasttodo = NULL; todo = method->tobedone;
	while (todo != NULL) {
		if (strcmp(todo->uri, uri) == 0)  {

			/* remove item: */
			if (lasttodo == NULL)
				method->tobedone = todo->next;
			else
				lasttodo->next = todo->next;
			if (method->nexttosend == todo) {
				/* just in case some method received
				 * files before we request them ;-) */
				method->nexttosend = todo->next;
			}
			if (method->lasttobedone == todo) {
				method->lasttobedone = todo->next;
			}
			if (todo->redirect_count < 10) {
				if (verbose > 0)
					fprintf(stderr,
"aptmethod redirects '%s' to '%s'\n",
						uri, newuri);
				/* readd with new uri */
				if (todo->original_uri != NULL)
					free(todo->uri);
				else
					todo->original_uri = todo->uri;
				todo->uri = newuri;
				todo->redirect_count++;
				enqueue(method, todo);
				return RET_OK;
			}
			fprintf(stderr,
"redirect loop (or too many redirects) detected, original uri is '%s'\n",
					todo->original_uri);
			/* put message in failed items to show it later? */
			free(newuri);
			return requeue_or_fail(method, todo);
		}
		lasttodo = todo;
		todo = todo->next;
	}
	/* huh? If if have not asked for it, how can there be errors? */
	fprintf(stderr,
"Method '%s' reported redirect for unrequested file '%s'-> '%s'\n",
			method->name, uri, newuri);
	free(newuri);
	return RET_ERROR;
}

/* look where a received file has to go to: */
static retvalue uridone(struct aptmethod *method, const char *uri, const char *filename, /*@only@*//*@null@*/struct checksums *checksumsfromapt) {
	struct tobedone *todo, *lasttodo;
	retvalue r;

	lasttodo = NULL; todo = method->tobedone;
	while (todo != NULL) {
		if (strcmp(todo->uri, uri) != 0)  {
			lasttodo = todo;
			todo = todo->next;
			continue;
		}

		r = todo->callback(qa_got,
			todo->privdata1, todo->privdata2,
			todo->original_uri? todo->original_uri : todo->uri,
			filename, todo->filename,
			checksumsfromapt, method->name);
		checksums_free(checksumsfromapt);

		/* remove item: */
		if (lasttodo == NULL)
			method->tobedone = todo->next;
		else
			lasttodo->next = todo->next;
		if (method->nexttosend == todo) {
			/* just in case some method received
			 * files before we request them ;-) */
			method->nexttosend = todo->next;
		}
		if (method->lasttobedone == todo) {
			method->lasttobedone = todo->next;
		}
		todo_free(todo);
		return r;
	}
	/* huh? */
	fprintf(stderr,
"Method '%s' retrieved unexpected file '%s' at '%s'!\n",
			method->name, uri, filename);
	checksums_free(checksumsfromapt);
	return RET_ERROR;
}

/***************************Input and Output****************************/
static retvalue logmessage(const struct aptmethod *method, const char *chunk, const char *type) {
	retvalue r;
	char *message;

	r = chunk_getvalue(chunk, "Message", &message);
	if (RET_WAS_ERROR(r))
		return r;
	if (RET_IS_OK(r)) {
		fprintf(stderr, "aptmethod '%s': '%s'\n",
				method->baseuri, message);
		free(message);
		return RET_OK;
	}
	r = chunk_getvalue(chunk, "URI", &message);
	if (RET_WAS_ERROR(r))
		return r;
	if (RET_IS_OK(r)) {
		fprintf(stderr, "aptmethod %s '%s'\n", type, message);
		free(message);
		return RET_OK;
	}
	fprintf(stderr, "aptmethod '%s': '%s'\n", method->baseuri, type);
	return RET_OK;
}
static inline retvalue gotcapabilities(struct aptmethod *method, const char *chunk) {
	retvalue r;

	r = chunk_gettruth(chunk, "Single-Instance");
	if (RET_WAS_ERROR(r))
		return r;
// TODO: what to do with this?
//	if (r != RET_NOTHING) {
//		fprintf(stderr, "WARNING: Single-instance not yet supported!\n");
//	}
	r = chunk_gettruth(chunk, "Send-Config");
	if (RET_WAS_ERROR(r))
		return r;
	if (r != RET_NOTHING) {
		assert(method->command == NULL);
		method->alreadywritten = 0;
		method->command = method->config;
		method->config = NULL;
		method->output_length = strlen(method->command);
		if (verbose > 11) {
			fprintf(stderr, "Sending config: '%s'\n",
					method->command);
		}
	} else {
		free(method->config);
		method->config = NULL;
	}
	method->status = ams_ok;
	return RET_OK;
}

static inline retvalue goturidone(struct aptmethod *method, const char *chunk) {
	static const char * const method_hash_names[cs_COUNT] =
		{ "MD5-Hash", "SHA1-Hash", "SHA256-Hash",
		  "Size" };
	retvalue result, r;
	char *uri, *filename;
	enum checksumtype type;
	char *hashes[cs_COUNT];
	struct checksums *checksums = NULL;

	//TODO: is it worth the mess to make this in-situ?

	r = chunk_getvalue(chunk, "URI", &uri);
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Missing URI header in uridone received from '%s' method!\n",
				method->name);
		r = RET_ERROR;
		method->status = ams_failed;
	}
	if (RET_WAS_ERROR(r))
		return r;

	r = chunk_getvalue(chunk, "Filename", &filename);
	if (r == RET_NOTHING) {
		char *altfilename;

		r = chunk_getvalue(chunk, "Alt-Filename", &altfilename);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Missing Filename header in uridone received from '%s' method!\n",
					method->name);
			r = urierror(method, uri, strdup(
"<no error but missing Filename from apt-method>"));
		} else {
			r = urierror(method, uri, mprintf(
"<File not there, apt-method suggests '%s' instead>", altfilename));
			free(altfilename);
		}
		free(uri);
		return r;
	}
	if (RET_WAS_ERROR(r)) {
		free(uri);
		return r;
	}
	if (verbose >= 1)
		fprintf(stderr, "aptmethod got '%s'\n", uri);

	result = RET_NOTHING;
	for (type = cs_md5sum ; type < cs_COUNT ; type++) {
		hashes[type] = NULL;
		r = chunk_getvalue(chunk, method_hash_names[type],
				&hashes[type]);
		RET_UPDATE(result, r);
	}
	if (RET_IS_OK(result) && hashes[cs_md5sum] == NULL) {
		/* the lenny version also has this, better ask for
		 * in case the old MD5-Hash vanishes in the future */
		r = chunk_getvalue(chunk, "MD5Sum-Hash", &hashes[cs_md5sum]);
		RET_UPDATE(result, r);
	}
	if (RET_WAS_ERROR(result)) {
		free(uri); free(filename);
		for (type = cs_md5sum ; type < cs_COUNT ; type++)
			free(hashes[type]);
		return result;
	}
	if (RET_IS_OK(result)) {
		/* ignore errors, we can recompute them from the file */
		(void)checksums_init(&checksums, hashes);
	}
	r = uridone(method, uri, filename, checksums);
	free(uri);
	free(filename);
	return r;
}

static inline retvalue goturierror(struct aptmethod *method, const char *chunk) {
	retvalue r;
	char *uri, *message;

	r = chunk_getvalue(chunk, "URI", &uri);
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Missing URI header in urierror received from '%s' method!\n", method->name);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;

	r = chunk_getvalue(chunk, "Message", &message);
	if (r == RET_NOTHING) {
		message = NULL;
	}
	if (RET_WAS_ERROR(r)) {
		free(uri);
		return r;
	}

	r = urierror(method, uri, message);
	free(uri);
	return r;
}

static inline retvalue gotredirect(struct aptmethod *method, const char *chunk) {
	char *uri, *newuri;
	retvalue r;

	r = chunk_getvalue(chunk, "URI", &uri);
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Missing URI header in uriredirect received from '%s' method!\n", method->name);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	r = chunk_getvalue(chunk, "New-URI", &newuri);
	if (r == RET_NOTHING) {
		fprintf(stderr,
"Missing New-URI header in uriredirect received from '%s' method!\n", method->name);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r)) {
		free(uri);
		return r;
	}
	r = uriredirect(method, uri, newuri);
	free(uri);
	return r;
}

static inline retvalue parsereceivedblock(struct aptmethod *method, const char *input) {
	const char *p;
	retvalue r;
#define OVERLINE {while (*p != '\0' && *p != '\n') p++; if (*p == '\n') p++; }

	while (*input == '\n' || *input == '\r')
		input++;
	if (*input == '\0') {
		fprintf(stderr,
"Unexpected number of newlines from '%s' method!\n", method->name);
		return RET_NOTHING;
	}
	p = input;
	switch ((*(input+1)=='0')?*input:'\0') {
		case '1':
			switch (*(input+2)) {
				/* 100 Capabilities */
				case '0':
					OVERLINE;
					if (verbose > 14) {
						fprintf(stderr, "Got '%s'\n",
								input);
					}
					return gotcapabilities(method, input);
				/* 101 Log */
				case '1':
					if (verbose > 10) {
						OVERLINE;
						return logmessage(method, p, "101");
					}
					return RET_OK;
				/* 102 Status */
				case '2':
					if (verbose > 5) {
						OVERLINE;
						return logmessage(method, p, "102");
					}
					return RET_OK;
				/* 103 Redirect */
				case '3':
					OVERLINE;
					return gotredirect(method, p);
				default:
					fprintf(stderr,
"Error or unsupported message received: '%s'\n",
							input);
					return RET_ERROR;
			}
		case '2':
			switch (*(input+2)) {
				/* 200 URI Start */
				case '0':
					if (verbose > 5) {
						OVERLINE;
						return logmessage(method, p, "start");
					}
					return RET_OK;
				/* 201 URI Done */
				case '1':
					OVERLINE;
					return goturidone(method, p);
				default:
					fprintf(stderr,
"Error or unsupported message received: '%s'\n",
							input);
					return RET_ERROR;
			}

		case '4':
			switch (*(input+2)) {
				case '0':
					OVERLINE;
					r = goturierror(method, p);
					break;
				case '1':
					OVERLINE;
					(void)logmessage(method, p, "general error");
					method->status = ams_failed;
					r = RET_ERROR;
					break;
				default:
					fprintf(stderr,
"Error or unsupported message received: '%s'\n",
							input);
					r = RET_ERROR;
			}
			/* a failed download is not a error yet, as it might
			 * be redone from another source later */
			return r;
		default:
			fprintf(stderr,
"Unexpected data from '%s' method: '%s'\n",
					method->name, input);
			return RET_ERROR;
	}
}

static retvalue receivedata(struct aptmethod *method) {
	retvalue result;
	ssize_t r;
	char *p;
	int consecutivenewlines;

	assert (method->status != ams_ok || method->tobedone != NULL);
	if (method->status != ams_waitforcapabilities
			&& method->status != ams_ok)
		return RET_NOTHING;

	/* First look if we have enough room to read.. */
	if (method->alreadyread + 1024 >= method->input_size) {
		char *newptr;

		if (method->input_size >= (size_t)128000) {
			fprintf(stderr,
"Ridiculously long answer from method!\n");
			method->status = ams_failed;
			return RET_ERROR;
		}

		newptr = realloc(method->inputbuffer, method->alreadyread+1024);
		if (FAILEDTOALLOC(newptr)) {
			return RET_ERROR_OOM;
		}
		method->inputbuffer = newptr;
		method->input_size = method->alreadyread + 1024;
	}
	assert (method->inputbuffer != NULL);
	/* then read as much as the pipe is able to fill of our buffer */

	r = read(method->mstdout, method->inputbuffer + method->alreadyread,
			method->input_size - method->alreadyread - 1);

	if (r < 0) {
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

		while (r > 0) {
			if (*p == '\0') {
				fprintf(stderr,
"Unexpected Zeroes in method output!\n");
				method->status = ams_failed;
				return RET_ERROR;
			} else if (*p == '\n') {
				consecutivenewlines++;
				if (consecutivenewlines >= 2)
					break;
			} else if (*p != '\r') {
				consecutivenewlines = 0;
			}
			p++; r--;
		}
		if (r <= 0) {
			return result;
		}
		*p ='\0'; p++; r--;
		res = parsereceivedblock(method, method->inputbuffer);
		if (r > 0)
			memmove(method->inputbuffer, p, r);
		method->alreadyread = r;
		RET_UPDATE(result, res);
	}
}

static retvalue senddata(struct aptmethod *method) {
	size_t l;
	ssize_t r;

	if (method->status != ams_ok)
		return RET_NOTHING;

	if (method->command == NULL) {
		const struct tobedone *todo;

		/* nothing queued to send, nothing to be queued...*/
		todo = method->nexttosend;
		if (todo == NULL)
			return RET_OK;

		if (interrupted())
			return RET_ERROR_INTERRUPTED;

		method->alreadywritten = 0;
		// TODO: make sure this is already checked for earlier...
		assert (strchr(todo->uri, '\n') == NULL &&
		        strchr(todo->filename, '\n') == NULL);
		/* http-aptmethod seems to loose the last byte,
		 * if the file is already in place,
		 * so we better unlink the target first...
		 * but this is done elsewhere already
		unlink(todo->filename);
		*/
		method->command = mprintf(
			 "600 URI Acquire\nURI: %s\nFilename: %s\n\n",
			 todo->uri, todo->filename);
		if (FAILEDTOALLOC(method->command)) {
			return RET_ERROR_OOM;
		}
		if (verbose > 20)
			fprintf(stderr, "Will sent: '%s'\n", method->command);
		method->output_length = strlen(method->command);
		method->nexttosend = method->nexttosend->next;
	}


	l = method->output_length - method->alreadywritten;

	r = write(method->mstdin, method->command + method->alreadywritten, l);
	if (r < 0) {
		int e = errno;

		fprintf(stderr, "Error %d writing to pipe: %s\n",
				e, strerror(e));
		//TODO: disable the whole method??
		method->status = ams_failed;
		return RET_ERRNO(e);
	} else if ((size_t)r < l) {
		method->alreadywritten += r;
		return RET_OK;
	}

	free(method->command);
	method->command = NULL;
	return RET_OK;
}

static retvalue checkchilds(struct aptmethodrun *run) {
	pid_t child;int status;
	retvalue result = RET_OK, r;

	while ((child = waitpid(-1, &status, WNOHANG)) > 0) {
		struct aptmethod *method;

		for (method = run->methods ; method != NULL ;
		                             method = method->next) {
			if (method->child == child)
				break;
		}
		if (method == NULL) {
			/* perhaps an uncompressor terminated */
			r = uncompress_checkpid(child, status);
			if (RET_IS_OK(r))
				continue;
			if (RET_WAS_ERROR(r)) {
				result = r;
				continue;
			}
			else {
				fprintf(stderr,
"Unexpected child died (maybe gpg died if signing/verifing was done): %d\n",
						(int)child);
				continue;
			}
		}
		/* Make sure we do not cope with this child any more */
		if (method->mstdin != -1) {
			(void)close(method->mstdin);
			method->mstdin = -1;
		}
		if (method->mstdout != -1) {
			(void)close(method->mstdout);
			method->mstdout = -1;
		}
		method->child = -1;
		if (method->status != ams_failed)
			method->status = ams_notstarted;

		/* say something if it exited unnormal: */
		if (WIFEXITED(status)) {
			int exitcode;

			exitcode = WEXITSTATUS(status);
			if (exitcode != 0) {
				fprintf(stderr,
"Method %s://%s exited with non-zero exit code %d!\n",
					method->name, method->baseuri,
					exitcode);
				method->status = ams_notstarted;
				result = RET_ERROR;
			}
		} else {
			fprintf(stderr, "Method %s://%s exited unnormally!\n",
					method->name, method->baseuri);
			method->status = ams_notstarted;
			result = RET_ERROR;
		}
	}
	return result;
}

/* *workleft is always set, even when return indicated error.
 * (workleft < 0 when critical)*/
static retvalue readwrite(struct aptmethodrun *run, /*@out@*/int *workleft) {
	int maxfd, v;
	fd_set readfds, writefds;
	struct aptmethod *method;
	retvalue result, r;

	/* First calculate what to look at: */
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	maxfd = 0;
	*workleft = 0;
	for (method = run->methods ; method != NULL ; method = method->next) {
		if (method->status == ams_ok &&
		    (method->command != NULL || method->nexttosend != NULL)) {
			FD_SET(method->mstdin, &writefds);
			if (method->mstdin > maxfd)
				maxfd = method->mstdin;
			(*workleft)++;
			if (verbose > 19)
				fprintf(stderr, "want to write to '%s'\n",
						method->baseuri);
		}
		if (method->status == ams_waitforcapabilities ||
				(method->status == ams_ok &&
				method->tobedone != NULL)) {
			FD_SET(method->mstdout, &readfds);
			if (method->mstdout > maxfd)
				maxfd = method->mstdout;
			(*workleft)++;
			if (verbose > 19)
				fprintf(stderr, "want to read from '%s'\n",
						method->baseuri);
		}
	}

	if (*workleft == 0)
		return RET_NOTHING;

	// TODO: think about a timeout...
	v = select(maxfd + 1, &readfds, &writefds, NULL, NULL);
	if (v < 0) {
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
	for (method = run->methods ; method != NULL ; method = method->next) {
		if (method->mstdout != -1 &&
				FD_ISSET(method->mstdout, &readfds)) {
			r = receivedata(method);
			RET_UPDATE(result, r);
		}
		if (method->mstdin != -1 &&
				FD_ISSET(method->mstdin, &writefds)) {
			r = senddata(method);
			RET_UPDATE(result, r);
		}
	}
	return result;
}

retvalue aptmethod_download(struct aptmethodrun *run) {
	struct aptmethod *method;
	retvalue result, r;
	int workleft;

	result = RET_NOTHING;

	/* fire up all methods, removing those that do not work: */
	for (method = run->methods; method != NULL ; method = method->next) {
		r = aptmethod_startup(method);
		/* do not remove failed methods here any longer,
		 * and not remove methods having nothing to do,
		 * as this breaks when no index files are downloaded
		 * due to all already being in place... */
		RET_UPDATE(result, r);
	}
	/* waiting for them to finish: */
	do {
	  r = checkchilds(run);
	  RET_UPDATE(result, r);
	  r = readwrite(run, &workleft);
	  RET_UPDATE(result, r);
	  // TODO: check interrupted here...
	} while (workleft > 0 || uncompress_running());

	return result;
}

