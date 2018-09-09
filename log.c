/*  This file is part of "reprepro"
 *  Copyright (C) 2007 Bernhard R. Link
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
#include <sys/poll.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include "error.h"
#include "strlist.h"
#include "atoms.h"
#include "dirs.h"
#include "target.h"
#include "distribution.h"
#include "configparser.h"
#include "log.h"
#include "filecntl.h"

/*@null@*/ static /*@refcounted@*/ struct logfile {
	/*@null@*/struct logfile *next;
	char *filename;
	/*@refs@*/size_t refcount;
	int fd;
} *logfile_root = NULL;

static retvalue logfile_reference(/*@only@*/char *filename, /*@out@*/struct logfile **logfile) {
	struct logfile *l;

	assert (global.logdir != NULL && filename != NULL);

	for (l = logfile_root ; l != NULL ; l = l->next) {
		if (strcmp(l->filename, filename) == 0) {
			l->refcount++;
			*logfile = l;
			free(filename);
			return RET_OK;
		}
	}
	l = NEW(struct logfile);
	if (FAILEDTOALLOC(l)) {
		free(filename);
		return RET_ERROR_OOM;
	}
	if (filename[0] == '/')
		l->filename = filename;
	else {
		l->filename = calc_dirconcat(global.logdir, filename);
		free(filename);
	}
	if (FAILEDTOALLOC(l->filename)) {
		free(l);
		return RET_ERROR_OOM;
	}
	l->refcount = 1;
	l->fd = -1;
	l->next = logfile_root;
	logfile_root = l;
	*logfile = l;
	return RET_OK;
}

static void logfile_dereference(struct logfile *logfile) {
	assert (logfile != NULL);
	assert (logfile->refcount > 0);
	if (--logfile->refcount == 0) {

		if (logfile_root == logfile)
			logfile_root = logfile->next;
		else {
			struct logfile *previous = logfile_root;

			while (previous != NULL && previous->next != logfile)
				previous = previous->next;
			assert (previous != NULL);
			assert (previous->next == logfile);
			previous->next = logfile->next;
		}
		if (logfile->fd >= 0) {
			int ret, e;

			ret = close(logfile->fd); logfile->fd = -1;
			if (ret < 0) {
				e = errno;
				fprintf(stderr,
"Error received when closing log file '%s': %d=%s\n",
					logfile->filename, e, strerror(e));
			}
		}
		free(logfile->filename);
		free(logfile);
	}
}

static retvalue logfile_open(struct logfile *logfile) {
	assert (logfile != NULL);
	assert (logfile->fd < 0);

	(void)dirs_make_parent(logfile->filename);
	logfile->fd = open(logfile->filename,
			O_CREAT|O_APPEND|O_NOCTTY|O_WRONLY,
			0666);
	if (logfile->fd < 0) {
		int e = errno;
		fprintf(stderr, "Cannot open/create logfile '%s': %d=%s\n",
				logfile->filename, e, strerror(e));
		return RET_ERRNO(e);
	}
	return RET_OK;
}

static retvalue logfile_write(struct logfile *logfile, struct target *target, const char *name, /*@null@*/const char *version, /*@null@*/const char *oldversion) {
	int ret;
	time_t currenttime;
	struct tm t;

	assert (logfile->fd >= 0);

	currenttime = time(NULL);
	if (localtime_r(&currenttime, &t) == NULL) {
		if (version != NULL && oldversion != NULL)
			ret = dprintf(logfile->fd,
"EEEE-EE-EE EE:EE:EE replace %s %s %s %s %s %s %s\n",
				target->distribution->codename,
				atoms_packagetypes[target->packagetype],
				atoms_components[target->component],
				atoms_architectures[target->architecture],
				name, version, oldversion);
		else if (version != NULL)
			ret = dprintf(logfile->fd,
"EEEE-EE-EE EE:EE:EE add %s %s %s %s %s %s\n",
				target->distribution->codename,
				atoms_packagetypes[target->packagetype],
				atoms_components[target->component],
				atoms_architectures[target->architecture],
				name, version);
		else
			ret = dprintf(logfile->fd,
"EEEE-EE-EE EE:EE:EE remove %s %s %s %s %s %s\n",
				target->distribution->codename,
				atoms_packagetypes[target->packagetype],
				atoms_components[target->component],
				atoms_architectures[target->architecture],
				name, oldversion);
	} else if (version != NULL && oldversion != NULL)
		ret = dprintf(logfile->fd,
"%04d-%02d-%02d %02u:%02u:%02u replace %s %s %s %s %s %s %s\n",
			1900+t.tm_year, t.tm_mon+1,
			t.tm_mday, t.tm_hour,
			t.tm_min, t.tm_sec,
			target->distribution->codename,
			atoms_packagetypes[target->packagetype],
			atoms_components[target->component],
			atoms_architectures[target->architecture],
			name, version, oldversion);
	else if (version != NULL)
		ret = dprintf(logfile->fd,
"%04d-%02d-%02d %02u:%02u:%02u add %s %s %s %s %s %s\n",
			1900+t.tm_year, t.tm_mon+1,
			t.tm_mday, t.tm_hour,
			t.tm_min, t.tm_sec,
			target->distribution->codename,
			atoms_packagetypes[target->packagetype],
			atoms_components[target->component],
			atoms_architectures[target->architecture],
			name, version);
	else
		ret = dprintf(logfile->fd,
"%04d-%02d-%02d %02u:%02u:%02u remove %s %s %s %s %s %s\n",
			1900+t.tm_year, t.tm_mon+1,
			t.tm_mday, t.tm_hour,
			t.tm_min, t.tm_sec,
			target->distribution->codename,
			atoms_packagetypes[target->packagetype],
			atoms_components[target->component],
			atoms_architectures[target->architecture],
			name, oldversion);
	if (ret < 0) {
		int e = errno;
		fprintf(stderr, "Error writing to log file '%s': %d=%s",
				logfile->filename, e, strerror(e));
		return RET_ERRNO(e);
	}
	return RET_OK;
}

struct notificator {
	char *scriptname;
	/* if defined, only call if it matches the package: */
	packagetype_t packagetype;
	component_t component;
	architecture_t architecture;
	command_t command;
	bool withcontrol, changesacceptrule;
};

static void notificator_done(/*@special@*/struct notificator *n) /*@releases n->scriptname, n->packagename, n->component, n->architecture@*/{
	free(n->scriptname);
}

static retvalue notificator_parse(struct notificator *n, struct configiterator *iter) {
	retvalue r;
	int c;

	setzero(struct notificator, n);
	n->architecture = atom_unknown;
	n->component = atom_unknown;
	n->packagetype = atom_unknown;
	n->command = atom_unknown;
	while ((c = config_nextnonspaceinline(iter)) != EOF) {
		if (c == '-') {
			char *word, *s, *detachedargument = NULL;
			const char *argument;
			atom_t *value_p = NULL;
			enum atom_type value_type;
			bool error = false;

			r = config_completeword(iter, c, &word);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r))
				return r;

			s = word + 1;
			while (*s != '\0' && *s != '=')
				s++;
			if (*s == '=') {
				argument = s+1;
				s[0] = '\0';
			} else
				argument = NULL;
			switch (s-word) {
				case 2:
					if (word[1] == 'A') {
						value_p = &n->architecture;
						value_type = at_architecture;
					} else if (word[1] == 'C') {
						value_p = &n->component;
						value_type = at_component;
					} else if (word[1] == 'T') {
						value_p = &n->packagetype;
						value_type = at_packagetype;
					} else
						error = true;
					break;
				case 5:
					if (strcmp(word, "--via") == 0) {
						value_p = &n->command;
						value_type = at_command;
					} else
						error = true;
					break;
				case 6:
					if (strcmp(word, "--type") == 0) {
						value_p = &n->packagetype;
						value_type = at_packagetype;
					} else
						error = true;
					break;
				case 9:
					if (strcmp(word, "--changes") == 0)
						n->changesacceptrule = true;
					else
						error = true;
					break;
				case 11:
					if (strcmp(word, "--component") == 0) {
						value_p = &n->component;
						value_type = at_component;
					} else
						error = true;
					break;
				case 13:
					if (strcmp(word, "--withcontrol") == 0)
						n->withcontrol = true;
					else
						error = true;
					break;
				case 14:
					if (strcmp(word, "--architecture") == 0) {
						value_p = &n->architecture;
						value_type = at_architecture;
					} else
						error = true;
					break;
				default:
					error = true;
					break;
			}
			if (error) {
				fprintf(stderr,
"Unknown Log notifier option in %s, line %u, column %u: '%s'\n",
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter), word);
				free(word);
				return RET_ERROR;
			}
			if (value_p == NULL) {
				if (argument != NULL) {
					fprintf(stderr,
"Log notifier option has = but may not, in %s, line %u, column %u: '%s'\n",
						config_filename(iter),
						config_markerline(iter),
						config_markercolumn(iter),
						word);
					free(word);
					return RET_ERROR;
				}
				free(word);
				continue;
			}
			/* option expecting string value: */
			if (atom_defined(*value_p)) {
				fprintf(stderr,
"Repeated notifier option %s in %s, line %u, column %u!\n", word,
					config_filename(iter),
					config_markerline(iter),
					config_markercolumn(iter));
				free(word);
				return RET_ERROR;
			}
			detachedargument = NULL;
			if (argument == NULL) {
				r = config_getwordinline(iter, &detachedargument);
				if (RET_WAS_ERROR(r))
					return r;
				if (r == RET_NOTHING) {
					fprintf(stderr,
"Log notifier option %s misses an argument in %s, line %u, column %u\n",
						word, config_filename(iter),
						config_line(iter),
						config_column(iter));
					free(word);
					return RET_ERROR;
				}
				argument = detachedargument;
			}
			*value_p = atom_find(value_type, argument);
			if (!atom_defined(*value_p)) {
				fprintf(stderr,
"Warning: unknown %s '%s', ignoring notificator line at line %u in %s\n",
					atomtypes[value_type],
					argument, config_line(iter),
					config_filename(iter));
				config_overline(iter);
				free(detachedargument);
				free(word);
				return RET_NOTHING;
			}
			free(detachedargument);
			free(word);
		} else {
			char *script;

			if (n->changesacceptrule && atom_defined(n->architecture)) {
				fprintf(stderr,
"Error: --changes and --architecture cannot be combined! (line %u in '%s')\n",
					config_markerline(iter), config_filename(iter));
				return RET_ERROR;
			}
			if (n->changesacceptrule && atom_defined(n->component)) {
				fprintf(stderr,
"Error: --changes and --component cannot be combined! (line %u in %s)\n",
					config_markerline(iter), config_filename(iter));
				return RET_ERROR;
			}
			if (n->changesacceptrule && atom_defined(n->packagetype)) {
				fprintf(stderr,
"Error: --changes and --type cannot be combined! (line %u in %s)\n",
					config_markerline(iter), config_filename(iter));
				return RET_ERROR;
			}

			r = config_completeword(iter, c, &script);
			assert (r != RET_NOTHING);
			if (RET_WAS_ERROR(r))
				return r;

			c = config_nextnonspaceinline(iter);
			if (c != EOF) {
				fprintf(stderr,
"Error parsing config file %s, line %u, column %u:\n"
"Unexpected data at end of notifier after script name '%s'\n",
					config_filename(iter),
					config_line(iter), config_column(iter),
					script);
				free(script);
				return RET_ERROR;
			}
			n->scriptname = configfile_expandname(script, script);
			if (FAILEDTOALLOC(n->scriptname))
				return RET_ERROR_OOM;
			return RET_OK;
		}
	}
	fprintf(stderr,
"Error parsing config file %s, line %u, column %u:\n"
"Unexpected end of line: name of notifier script missing!\n",
		config_filename(iter), config_line(iter), config_column(iter));
	return RET_ERROR;
}

/*@null@*/ static struct notification_process {
	/*@null@*/struct notification_process *next;
	char **arguments;
	/*@null@*/char *causingfile;
	/*@null@*/char *causingrule;
	/*@null@*/char *suitefrom;
	/* data to send to the process */
	size_t datalen, datasent;
	/*@null@*/char *data;
	/* process */
	pid_t child;
	int fd;
} *processes = NULL;

static void notification_process_free(/*@only@*/struct notification_process *p) {
	char **a;

	if (p->fd >= 0)
		(void)close(p->fd);
	for (a = p->arguments ; *a != NULL ; a++)
		free(*a);
	free(p->causingfile);
	free(p->causingrule);
	free(p->suitefrom);
	free(p->arguments);
	free(p->data);
	free(p);
}

static int catchchildren(void) {
	pid_t child;
	int status;
	struct notification_process *p, **pp;
	int returned = 0;

	/* to avoid stealing aptmethods.c children, only
	 * check for our children. (As not many run, that
	 * is no large overhead. */
	pp = &processes;
	while ((p=*pp) != NULL) {
		if (p->child <= 0) {
			pp = &p->next;
			continue;
		}

		child = waitpid(p->child, &status, WNOHANG);
		if (child == 0) {
			pp = &p->next;
			continue;
		}
		if (child < 0) {
			int e = errno;
			fprintf(stderr,
"Error calling waitpid on notification child: %d=%s\n",
					e, strerror(e));
			/* but still handle the failed child: */
		} else if (WIFSIGNALED(status)) {
			fprintf(stderr,
"Notification process '%s' killed with signal %d!\n",
					p->arguments[0], WTERMSIG(status));
		} else if (!WIFEXITED(status)) {
			fprintf(stderr,
"Notification process '%s' failed!\n",
					p->arguments[0]);
		} else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
			fprintf(stderr,
"Notification process '%s' returned with exit code %d!\n",
					p->arguments[0],
					(int)(WEXITSTATUS(status)));
		}
		if (p->fd >= 0) {
			(void)close(p->fd);
			p->fd = -1;
		}
		p->child = 0;
		*pp = p->next;
		notification_process_free(p);
		returned++;
	}
	return returned;
}

static void feedchildren(bool dowait) {
	struct notification_process *p;
	fd_set w;
	int ret;
	int number = 0;
	struct timeval tv = {0, 0};

	FD_ZERO(&w);
	for (p = processes; p!= NULL ; p = p->next) {
		if (p->child > 0 && p->fd >= 0 && p->datasent < p->datalen) {
			FD_SET(p->fd, &w);
			if (p->fd >= number)
				number = p->fd + 1;
		}
	}
	if (number == 0)
		return;
	ret = select(number, NULL, &w, NULL, dowait?NULL:&tv);
	if (ret < 0) {
		// TODO...
		return;
	}
	for (p = processes; p != NULL ; p = p->next) {
		if (p->child > 0 && p->fd >= 0 && FD_ISSET(p->fd, &w)) {
			size_t tosent = p->datalen - p->datasent;
			ssize_t sent;

			if (tosent > (size_t)512)
				tosent = 512;
			sent = write(p->fd, p->data+p->datasent, 512);
			if (sent < 0) {
				int e = errno;
				fprintf(stderr,
"Error '%s' while sending data to '%s', sending SIGABRT to it!\n",
						strerror(e),
						p->arguments[0]);
				(void)kill(p->child, SIGABRT);
			}
			p->datasent += sent;
			if (p->datasent >= p->datalen) {
				free(p->data);
				p->data = NULL;
			}
		}
	}
}

static size_t runningchildren(void) {
	struct notification_process *p;
	size_t running = 0;

	p = processes;
	while (p != NULL && p->child != 0) {
		running ++;
		p = p->next;
	}
	return running;
}

static retvalue startchild(void) {
	struct notification_process *p;
	pid_t child;
	int filedes[2];
	int ret;

	p = processes;
	while (p != NULL && p->child != 0)
		p = p->next;
	if (p == NULL)
		return RET_NOTHING;
	if (p->datalen > 0) {
		ret = pipe(filedes);
		if (ret < 0) {
			int e = errno;
			fprintf(stderr, "Error creating pipe: %d=%s!\n",
					e, strerror(e));
			return RET_ERRNO(e);
		}
		p->fd = filedes[1];
	} else {
		p->fd = -1;
	}
	child = fork();
	if (child == 0) {
		if (p->datalen > 0) {
			dup2(filedes[0], 0);
			if (filedes[0] != 0)
				(void)close(filedes[0]);
			(void)close(filedes[1]);
		}
		/* Try to close all open fd but 0,1,2 */
		closefrom(3);
		sethookenvironment(p->causingfile, p->causingrule,
				p->suitefrom, NULL);
		(void)execv(p->arguments[0], p->arguments);
		fprintf(stderr, "Error executing '%s': %s\n", p->arguments[0],
				strerror(errno));
		_exit(255);
	}
	if (p->datalen > 0) {
		(void)close(filedes[0]);
		markcloseonexec(p->fd);
	}
	if (child < 0) {
		int e = errno;
		fprintf(stderr, "Error forking: %d=%s!\n", e, strerror(e));
		if (p->fd >= 0) {
			(void)close(p->fd);
			p->fd = -1;
		}
		return RET_ERRNO(e);
	}
	p->child = child;
	if (p->datalen > 0) {
		struct pollfd polldata;
		ssize_t written;

		polldata.fd = p->fd;
		polldata.events = POLLOUT;
		while (poll(&polldata, 1, 0) > 0) {
			if ((polldata.revents & POLLNVAL) != 0) {
				p->fd = -1;
				return RET_ERROR;
			}
			if ((polldata.revents & POLLHUP) != 0) {
				(void)close(p->fd);
				p->fd = -1;
				return RET_OK;
			}
			if ((polldata.revents & POLLOUT) != 0) {
				size_t towrite =  p->datalen - p->datasent;
				if (towrite > (size_t)512)
					towrite = 512;
				written = write(p->fd,
						p->data + p->datasent,
						towrite);
				if (written < 0) {
					int e = errno;
					fprintf(stderr,
"Error '%s' while sending data to '%s', sending SIGABRT to it!\n",
							strerror(e),
							p->arguments[0]);
					(void)kill(p->child, SIGABRT);
					return RET_ERRNO(e);
				}
				p->datasent += written;
				if (p->datasent >= p->datalen) {
					free(p->data);
					p->data = NULL;
					ret = close(p->fd);
					p->fd = -1;
					if (ret != 0)
						return RET_ERRNO(errno);
					else
						return RET_OK;
				}
				continue;
			}
			/* something to write but at the same time not,
			 * let's better stop here better */
			return RET_OK;
		}
	}
	return RET_OK;
}

static retvalue notificator_enqueuechanges(struct notificator *n, const char *codename, const char *name, const char *version, const char *safefilename, /*@null@*/const char *filekey) {
	size_t count, i, j;
	char **arguments;
	struct notification_process *p;

	catchchildren();
	feedchildren(false);
	if (!n->changesacceptrule)
		return RET_NOTHING;
	if (limitation_missed(n->command, causingcommand)) {
		return RET_NOTHING;
	}
	count = 6; /* script "accepted" codename name version safename */
	if (filekey != NULL)
		count++;
	arguments = nzNEW(count + 1, char*);
	if (FAILEDTOALLOC(arguments))
		return RET_ERROR_OOM;
	i = 0;
	arguments[i++] = strdup(n->scriptname);
	arguments[i++] = strdup("accepted");
	arguments[i++] = strdup(codename);
	arguments[i++] = strdup(name);
	arguments[i++] = strdup(version);
	arguments[i++] = strdup(safefilename);
	if (filekey != NULL)
		arguments[i++] = strdup(filekey);
	assert (i == count);
	arguments[i] = NULL;
	for (i = 0 ; i < count ; i++)
		if (FAILEDTOALLOC(arguments[i])) {
			for (j = 0 ; j < count ; j++)
				free(arguments[j]);
			free(arguments);
			return RET_ERROR_OOM;
		}
	if (processes == NULL) {
		p = NEW(struct notification_process);
		processes = p;
	} else {
		p = processes;
		while (p->next != NULL)
			p = p->next;
		p->next = NEW(struct notification_process);
		p = p->next;
	}
	if (FAILEDTOALLOC(p)) {
		for (j = 0 ; j < count ; j++)
			free(arguments[j]);
		free(arguments);
		return RET_ERROR_OOM;
	}
	if (causingfile != NULL) {
		p->causingfile = strdup(causingfile);
		if (FAILEDTOALLOC(p->causingfile)) {
			for (j = 0 ; j < count ; j++)
				free(arguments[j]);
			free(arguments);
			free(p);
			return RET_ERROR_OOM;
		}
	} else
		p->causingfile = NULL;
	p->causingrule = NULL;
	p->suitefrom = NULL;
	p->arguments = arguments;
	p->next = NULL;
	p->child = 0;
	p->fd = -1;
	p->datalen = 0;
	p->datasent = 0;
	p->data = NULL;

	if (runningchildren() < 1)
		startchild();
	return RET_OK;
}

static retvalue notificator_enqueue(struct notificator *n, struct target *target, const char *name, /*@null@*/const char *version, /*@null@*/const char *oldversion, /*@null@*/const struct strlist *filekeys, /*@null@*/const struct strlist *oldfilekeys, bool renotification, /*@null@*/const char *causingrule, /*@null@*/ const char *suitefrom) {
	size_t count, i;
	char **arguments;
	const char *action = NULL;
	struct notification_process *p;

	catchchildren();
	feedchildren(false);
	if (n->changesacceptrule)
		return RET_NOTHING;
	// some day, some atom handling for those would be nice
	if (limitation_missed(n->architecture, target->architecture)) {
		if (runningchildren() < 1)
			startchild();
		return RET_NOTHING;
	}
	if (limitation_missed(n->component, target->component)) {
		if (runningchildren() < 1)
			startchild();
		return RET_NOTHING;
	}
	if (limitation_missed(n->packagetype, target->packagetype)) {
		if (runningchildren() < 1)
			startchild();
		return RET_NOTHING;
	}
	if (limitation_missed(n->command, causingcommand)) {
		if (runningchildren() < 1)
			startchild();
		return RET_NOTHING;
	}
	count = 7; /* script action codename type component architecture */
	if (version != NULL) {
		action = "add";
		count += 2; /* version and filekeylist marker */
		if (filekeys != NULL)
			count += filekeys->count;
	}
	if (oldversion != NULL) {
		assert (!renotification);

		if (action == NULL)
			action = "remove";
		else
			action = "replace";

		count += 2; /* version and filekeylist marker */
		if (oldfilekeys != NULL)
			count += oldfilekeys->count;
	}
	assert (action != NULL);
	if (renotification)
		action = "info";
	arguments = nzNEW(count + 1, char*);
	if (FAILEDTOALLOC(arguments))
		return RET_ERROR_OOM;
	i = 0;
	arguments[i++] = strdup(n->scriptname);
	arguments[i++] = strdup(action);
	arguments[i++] = strdup(target->distribution->codename);
	arguments[i++] = strdup(atoms_packagetypes[target->packagetype]);
	arguments[i++] = strdup(atoms_components[target->component]);
	arguments[i++] = strdup(atoms_architectures[target->architecture]);
	arguments[i++] = strdup(name);
	if (version != NULL)
		arguments[i++] = strdup(version);
	if (oldversion != NULL)
		arguments[i++] = strdup(oldversion);
	if (version != NULL) {
		int j;
		arguments[i++] = strdup("--");
		if (filekeys != NULL)
			for (j = 0 ; j < filekeys->count ; j++)
				arguments[i++] = strdup(filekeys->values[j]);
	}
	if (oldversion != NULL) {
		int j;
		arguments[i++] = strdup("--");
		if (oldfilekeys != NULL)
			for (j = 0 ; j < oldfilekeys->count ; j++)
				arguments[i++] = strdup(oldfilekeys->values[j]);
	}
	assert (i == count);
	arguments[i] = NULL;
	for (i = 0 ; i < count ; i++) {
		size_t j;
		if (FAILEDTOALLOC(arguments[i])) {
			for (j = 0 ; j < count ; j++)
				free(arguments[j]);
			free(arguments);
			return RET_ERROR_OOM;
		}
	}
	if (processes == NULL) {
		p = NEW(struct notification_process);
		processes = p;
	} else {
		p = processes;
		while (p->next != NULL)
			p = p->next;
		p->next = NEW(struct notification_process);
		p = p->next;
	}
	if (FAILEDTOALLOC(p)) {
		size_t j;
		for (j = 0 ; j < count ; j++)
			free(arguments[j]);
		free(arguments);
		return RET_ERROR_OOM;
	}
	if (causingfile != NULL) {
		size_t j;
		p->causingfile = strdup(causingfile);
		if (FAILEDTOALLOC(p->causingfile)) {
			for (j = 0 ; j < count ; j++)
				free(arguments[j]);
			free(arguments);
			free(p);
			return RET_ERROR_OOM;
		}
	} else
		p->causingfile = NULL;
	if (causingrule != NULL) {
		size_t j;
		p->causingrule = strdup(causingrule);
		if (FAILEDTOALLOC(p->causingrule)) {
			for (j = 0 ; j < count ; j++)
				free(arguments[j]);
			free(arguments);
			free(p->causingfile);
			free(p);
			return RET_ERROR_OOM;
		}
	} else
		p->causingrule = NULL;
	if (suitefrom != NULL) {
		size_t j;
		p->suitefrom = strdup(suitefrom);
		if (FAILEDTOALLOC(p->suitefrom)) {
			for (j = 0 ; j < count ; j++)
				free(arguments[j]);
			free(arguments);
			free(p->causingfile);
			free(p->causingrule);
			free(p);
			return RET_ERROR_OOM;
		}
	} else
		p->suitefrom = NULL;
	p->arguments = arguments;
	p->next = NULL;
	p->child = 0;
	p->fd = -1;
	p->datalen = 0;
	p->datasent = 0;
	p->data = NULL;
	if (runningchildren() < 1)
		startchild();
	return RET_OK;
}

void logger_wait(void) {
	while (processes != NULL) {
		catchchildren();
		if (interrupted())
			break;
		feedchildren(true);
		// TODO: add option to start multiple at the same time
		if (runningchildren() < 1)
			startchild();
		else {
			struct timeval tv = { 0, 100 };
			select(0, NULL, NULL, NULL, &tv);
		}
	}
}

void logger_warn_waiting(void) {
	struct notification_process *p;

	if (processes != NULL) {
		(void)fputs(
"WARNING: some notificator hooks were not run!\n"
"(most likely due to receiving an interruption request)\n"
"You will either have to run them by hand or run rerunnotifiers if\n"
"you want the information they get to not be out of sync.\n"
"Missed calls are:\n", stderr);
		for (p = processes ; p != NULL ; p = p->next) {
			char **c = p->arguments;
			if (c == NULL)
				continue;
			while (*c != NULL) {
				(void)fputc('"', stderr);
				(void)fputs(*c, stderr);
				(void)fputc('"', stderr);
				c++;
				if (*c != NULL)
					(void)fputc(' ', stderr);
			}
			(void)fputc('\n', stderr);
		}
	}
}

struct logger {
	/*@dependent@*//*@null@*/struct logfile *logfile;
	size_t notificator_count;
	struct notificator *notificators;
};

void logger_free(struct logger *logger) {
	if (logger == NULL)
		return;

	if (logger->logfile != NULL)
		logfile_dereference(logger->logfile);
	if (logger->notificators != NULL) {
		size_t i;

		for (i = 0 ; i < logger->notificator_count ; i++)
			notificator_done(&logger->notificators[i]);
		free(logger->notificators);
	}

	free(logger);
}

retvalue logger_init(struct configiterator *iter, struct logger **logger_p) {
	struct logger *n;
	retvalue r;
	char *logfilename;
	bool havenotificators;

	r = config_getfileinline(iter, &logfilename);
	if (RET_WAS_ERROR(r))
		return r;
	if (r == RET_NOTHING)
		logfilename = NULL;
	if (config_nextnonspaceinline(iter) != EOF) {
		fprintf(stderr, "Error parsing %s, line %u, column %u:\n"
				"Unexpected second filename for logfile.\n",
				config_filename(iter), config_line(iter),
				config_column(iter));
		free(logfilename);
		return RET_ERROR;
	}
	config_overline(iter);
	havenotificators = config_nextline(iter);
	if (!havenotificators && logfilename == NULL) {
		*logger_p = NULL;
		return RET_NOTHING;
	}

	n = NEW(struct logger);
	if (FAILEDTOALLOC(n)) {
		free(logfilename);
		return RET_ERROR_OOM;
	}
	if (logfilename != NULL) {
		assert (*logfilename != '\0');
		r = logfile_reference(logfilename, &n->logfile);
		if (RET_WAS_ERROR(r)) {
			free(n);
			return r;
		}
	} else
		n->logfile = NULL;

	n->notificators = NULL;
	n->notificator_count = 0;

	while (havenotificators) {
		struct notificator *newnot;
		newnot = realloc(n->notificators,
				(n->notificator_count+1)
				* sizeof(struct notificator));
		if (FAILEDTOALLOC(newnot)) {
			logger_free(n);
			return RET_ERROR_OOM;
		}
		n->notificators = newnot;
		r = notificator_parse(&n->notificators[n->notificator_count++],
				 iter);
		if (RET_WAS_ERROR(r)) {
			/* a bit ugly: also free the just failed item here */
			logger_free(n);
			return r;
		}
		if (r == RET_NOTHING)
			n->notificator_count--;
		// TODO assert eol here...
		havenotificators = config_nextline(iter);
	}
	*logger_p = n;
	return RET_OK;
}

retvalue logger_prepare(struct logger *logger) {
	retvalue r;

	if (logger->logfile == NULL)
		return RET_NOTHING;

	if (logger->logfile != NULL && logger->logfile->fd < 0) {
		r = logfile_open(logger->logfile);
	} else
		r = RET_OK;
	return r;
}
bool logger_isprepared(/*@null@*/const struct logger *logger) {
	if (logger == NULL)
		return true;
	if (logger->logfile != NULL && logger->logfile->fd < 0)
		return false;
	return true;
}

void logger_log(struct logger *log, struct target *target, const char *name, const char *version, const char *oldversion, const struct strlist *filekeys, const struct strlist *oldfilekeys, const char *causingrule, const char *suitefrom) {
	size_t i;

	assert (name != NULL);

	assert (version != NULL || oldversion != NULL);

	if (log->logfile != NULL)
		logfile_write(log->logfile, target, name, version, oldversion);
	for (i = 0 ; i < log->notificator_count ; i++) {
		notificator_enqueue(&log->notificators[i], target,
				name, version, oldversion,
				filekeys, oldfilekeys, false,
				causingrule, suitefrom);
	}
}

void logger_logchanges(struct logger *log, const char *codename, const char *name, const char *version, const char *safefilename, const char *changesfilekey) {
	size_t i;

	assert (name != NULL);
	assert (version != NULL);

	if (log == NULL)
		return;

	for (i = 0 ; i < log->notificator_count ; i++) {
		notificator_enqueuechanges(&log->notificators[i], codename,
				name, version, safefilename,
				changesfilekey);
	}
}

bool logger_rerun_needs_target(const struct logger *logger, const struct target *target) {
	size_t i;
	struct notificator *n;

	for (i = 0 ; i < logger->notificator_count ; i++) {
		n = &logger->notificators[i];

		if (limitation_missed(n->architecture, target->architecture))
			continue;
		if (limitation_missed(n->component, target->component))
			continue;
		if (limitation_missed(n->packagetype, target->packagetype))
			continue;
		return true;
	}
	return false;
}

retvalue logger_reruninfo(struct logger *logger, struct target *target, const char *name, const char *version, /*@null@*/const struct strlist *filekeys) {
	retvalue result, r;
	size_t i;

	assert (name != NULL);
	assert (version != NULL);

	result = RET_NOTHING;

	for (i = 0 ; i < logger->notificator_count ; i++) {
		r = notificator_enqueue(&logger->notificators[i], target,
				name, version, NULL,
				filekeys, NULL, true,
				NULL, NULL);
		RET_UPDATE(result, r);
	}
	return result;
}
