/*  This file is part of "reprepro"
 *  Copyright (C) 2012 Bernhard R. Link
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
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "error.h"
#include "filecntl.h"
#include "mprintf.h"
#include "strlist.h"
#include "dirs.h"
#include "hooks.h"
#include "outhook.h"

static FILE *outlogfile = NULL;
static char *outlogfilename = NULL;
static bool outlognonempty = false;

retvalue outhook_start(void) {
	retvalue r;
	int fd;
	char *template;

	assert (outlogfilename == NULL);
	assert (outlogfile == NULL);

	r = dirs_create(global.logdir);
	if (RET_WAS_ERROR(r))
		return r;

	template = mprintf("%s/%010llu-XXXXXX.outlog",
			global.logdir, (unsigned long long)time(NULL));
	if (FAILEDTOALLOC(template))
		return RET_ERROR_OOM;
	fd = mkstemps(template, 7);
	if (fd < 0) {
		int e = errno;
		fprintf(stderr, "Error %d creating new file in %s: %s\n",
				e, global.logdir, strerror(e));
		free(template);
		return RET_ERRNO(e);
	}
	outlogfile = fdopen(fd, "w");
	if (outlogfile == NULL) {
		int e = errno;
		(void)close(fd);
		fprintf(stderr, "Error %d from fdopen: %s\n",
				e, strerror(e));
		free(template);
		return RET_ERRNO(e);
	}
	outlogfilename = template;
	return RET_OK;
}

void outhook_send(const char *command, const char *arg1, const char *arg2, const char *arg3) {
	assert (command != NULL);
	assert (arg1 != NULL);
	assert (arg3 == NULL || arg2 != NULL);
	if (outlogfile == NULL)
		return;

	if (arg2 == NULL)
		fprintf(outlogfile, "%s\t%s\n", command, arg1);
	else if (arg3 == NULL)
		fprintf(outlogfile, "%s\t%s\t%s\n", command, arg1, arg2);
	else
		fprintf(outlogfile, "%s\t%s\t%s\t%s\n", command,
				arg1, arg2, arg3);
	outlognonempty = true;
}

void outhook_sendpool(component_t component, const char *sourcename, const char *name) {
	assert (name != NULL);
	if (outlogfile == NULL)
		return;
	if (sourcename == NULL || *sourcename == '\0')
		fprintf(outlogfile, "POOLNEW\t%s\n", name);
	else if (sourcename[0] == 'l' && sourcename[1] == 'i' &&
			sourcename[2] == 'b' && sourcename[3] != '\0')
		fprintf(outlogfile, "POOLNEW\tpool/%s/lib%c/%s/%s\n",
				atoms_components[component],
				sourcename[3], sourcename, name);
	else
		fprintf(outlogfile, "POOLNEW\tpool/%s/%c/%s/%s\n",
				atoms_components[component],
				sourcename[0], sourcename, name);
	outlognonempty = true;
}

static retvalue callouthook(const char *scriptname, const char *logfilename) {
	pid_t child;

	child = fork();
	if (child == 0) {
		/* Try to close all open fd but 0,1,2 */
		closefrom(3);
		sethookenvironment(causingfile, NULL, NULL, NULL);
		(void)execl(scriptname, scriptname, logfilename, (char*)NULL);
		{
			int e = errno;
			fprintf(stderr, "Error %d executing '%s': %s\n",
					e, scriptname, strerror(e));
		}
		_exit(255);
	}
	if (child < 0) {
		int e = errno;
		fprintf(stderr, "Error %d forking: %s!\n", e, strerror(e));
		return RET_ERRNO(e);
	}
	while (true) {
		int status;
		pid_t pid;

		pid = waitpid(child, &status, 0);
		if (pid == child) {
			if (WIFEXITED(status)) {
				if (WEXITSTATUS(status) == 0) {
					return RET_OK;
				}
				fprintf(stderr,
"Outhook '%s' '%s' failed with exit code %d!\n",
					scriptname, logfilename,
					(int)(WEXITSTATUS(status)));
			} else if (WIFSIGNALED(status)) {
				fprintf(stderr,
"Outhook '%s' '%s' killed by signal %d!\n",
					scriptname, logfilename,
					(int)(WTERMSIG(status)));
			} else {
				fprintf(stderr,
"Outhook '%s' '%s' failed!\n",
					scriptname, logfilename);
			}
			return RET_ERROR;
		} else if (pid == (pid_t)-1) {
			int e = errno;

			if (e == EINTR)
				continue;
			fprintf(stderr,
"Error %d calling waitpid on outhook child: %s\n",
				e, strerror(e));
			return RET_ERRNO(e);
		}
	}
	/* NOT REACHED */
}

retvalue outhook_call(const char *scriptname) {
	retvalue result;

	assert (outlogfile != NULL);
	assert (outlogfilename != NULL);

	if (ferror(outlogfile) != 0) {
		(void)fclose(outlogfile);
		fprintf(stderr, "Errors creating '%s'!\n",
				outlogfilename);
		result = RET_ERROR;
	} else if (fclose(outlogfile) != 0) {
		fprintf(stderr, "Errors creating '%s'!\n",
				outlogfilename);
		result = RET_ERROR;
	} else if (!outlognonempty) {
		unlink(outlogfilename);
		result = RET_OK;
	} else {
		result = callouthook(scriptname, outlogfilename);
	}
	outlogfile = NULL;
	free(outlogfilename);
	outlogfilename = NULL;
	return result;
}
