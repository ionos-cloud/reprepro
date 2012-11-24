/*  This file is part of "reprepro"
 *  Copyright (C) 2010 Bernhard R. Link
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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "error.h"
#include "filecntl.h"
#include "names.h"
#include "configparser.h"
#include "globmatch.h"
#include "hooks.h"
#include "byhandhook.h"

struct byhandhook {
	/*@null@*/struct byhandhook *next;
	char *sectionglob;
	char *priorityglob;
	char *filenameglob;
	char *script;
};

void byhandhooks_free(struct byhandhook *l) {
	while (l != NULL) {
		/*@null@*/struct byhandhook *n = l->next;

		free(l->sectionglob);
		free(l->priorityglob);
		free(l->filenameglob);
		free(l->script);
		free(l);
		l = n;
	}
}

retvalue byhandhooks_parse(struct configiterator *iter, struct byhandhook **hooks_p) {
	retvalue r;
	char *v;
	struct byhandhook *h, *hooks = NULL, **nexthook_p = &hooks;

	r = config_getwordinline(iter, &v);
	if (RET_IS_OK(r)) {
		fprintf(stderr,
"Error parsing %s, line %u, column %u: unexpected input '%s'"
" (each hook must be in its own line)!\n",
				config_filename(iter),
				config_markerline(iter),
				config_markercolumn(iter),
				v);
		free(v);
		r = RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;
	while (config_nextline(iter)) {
		r = config_getwordinline(iter, &v);
		if (r == RET_NOTHING)
			continue;
		if (RET_WAS_ERROR(r))
			break;
		h = zNEW(struct byhandhook);
		if (FAILEDTOALLOC(h)) {
			r = RET_ERROR_OOM;
			break;
		}
		*nexthook_p = h;
		nexthook_p = &h->next;
		h->sectionglob = v;
		r = config_getwordinline(iter, &v);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Error parsing %s, line %u, column %u: each byhand hooks needs 4 arguments, found only 1!\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter));
			r = RET_ERROR;
		}
		if (RET_WAS_ERROR(r))
			break;
		h->priorityglob = v;
		r = config_getwordinline(iter, &v);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Error parsing %s, line %u, column %u: each byhand hooks needs 4 arguments, found only 2!\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter));
			r = RET_ERROR;
		}
		if (RET_WAS_ERROR(r))
			break;
		h->filenameglob = v;
		r = config_getwordinline(iter, &v);
		if (r == RET_NOTHING) {
			fprintf(stderr,
"Error parsing %s, line %u, column %u: each byhand hooks needs 4 arguments, found only 2!\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter));
			r = RET_ERROR;
		}
		if (RET_WAS_ERROR(r))
			break;
		assert (v != NULL && v[0] != '\0'); \
		h->script = configfile_expandname(v, v);
		if (FAILEDTOALLOC(h->script)) {
			r = RET_ERROR_OOM;
			break;
		}
		r = config_getwordinline(iter, &v);
		if (RET_IS_OK(r)) {
			fprintf(stderr,
"Error parsing %s, line %u, column %u: each byhand hooks needs exactly 4 arguments, but there are more (first unexpected: '%s'!\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter), v);
			free(v);
			r = RET_ERROR;
		}
		if (RET_WAS_ERROR(r))
			break;
	}
	if (RET_WAS_ERROR(r)) {
		byhandhooks_free(hooks);
		return r;
	}
	*hooks_p = hooks;
	return RET_OK;
}

bool byhandhooks_matched(const struct byhandhook *list, const struct byhandhook **touse, const char *section, const char *priority, const char *filename) {
	const struct byhandhook *h;

	/* for each file the first matching hook is called
	 * it might later be extended to allow multiple with some keywords */
	if (*touse != NULL)
		/* if ((*touse)->nonexclusive) list = (*touse)->next ; else */
		return false;
	for (h = list ; h != NULL ; h = h->next) {
		if (!globmatch(section, h->sectionglob))
			continue;
		if (!globmatch(priority, h->priorityglob))
			continue;
		if (!globmatch(filename, h->filenameglob))
			continue;
		*touse = h;
		return true;
	}
	return false;
}

retvalue byhandhook_call(const struct byhandhook *h, const char *codename, const char *section, const char *priority, const char *name, const char *fullfilename) {
	pid_t child;

	child = fork();
	if (child == 0) {
		/* Try to close all open fd but 0,1,2 */
		closefrom(3);
		sethookenvironment(causingfile, NULL, NULL, NULL);
		(void)execl(h->script, h->script, codename,
				section, priority, name,
				fullfilename, (char*)NULL);
		{
			int e = errno;
			fprintf(stderr, "Error %d executing '%s': %s\n",
					e, h->script,
				strerror(e));
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
"Byhandhook '%s' '%s' '%s' '%s' '%s' '%s' failed with exit code %d!\n",
					h->script, codename,
					section, priority, name,
					fullfilename,
					(int)(WEXITSTATUS(status)));
			} else if (WIFSIGNALED(status)) {
				fprintf(stderr,
"Byhandhook '%s' '%s' '%s' '%s' '%s' '%s' killed by signal %d!\n",
					h->script, codename,
					section, priority, name,
					fullfilename,
					(int)(WTERMSIG(status)));
			} else {
				fprintf(stderr,
"Byhandhook '%s' '%s' '%s' '%s' '%s' '%s' failed!\n",
					h->script, codename,
					section, priority, name,
					fullfilename);
			}
			return RET_ERROR;
		} else if (pid == (pid_t)-1) {
			int e = errno;

			if (e == EINTR)
				continue;
			fprintf(stderr,
"Error %d calling waitpid on byhandhook child: %s\n",
				e, strerror(e));
			return RET_ERRNO(e);
		}
	}
	/* NOT REACHED */
}

