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
#include <stdio.h>
#include <time.h>
#include <malloc.h>
#include "error.h"
#include "strlist.h"
#include "dirs.h"
#include "target.h"
#include "log.h"

extern int verbose;

/*@null@*/ static struct logfile {
	struct logfile *next;
	char *filename;
	size_t refcount;
	int fd;
} *logfile_root = NULL;

static retvalue logfile_reference(const char *logdir,const char *filename,/*@out@*/struct logfile **logfile) {
	struct logfile *l;

	assert( logdir != NULL && filename != NULL );

	for( l = logfile_root ; l != NULL ; l = l->next ) {
		if( strcmp(l->filename, filename) == 0 ) {
			l->refcount++;
			*logfile = l;
			return RET_OK;
		}
	}
	l = malloc(sizeof(struct logfile));
	if( l == NULL )
		return RET_ERROR_OOM;
	if( filename[0] == '/' )
		l->filename = strdup(filename);
	else
		l->filename = calc_dirconcat(logdir,filename);
	if( l->filename == NULL ) {
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
	assert( logfile != NULL );
	assert( logfile->refcount > 0 );
	if( --logfile->refcount == 0 ) {

		if( logfile_root == logfile )
			logfile_root = logfile->next;
		else {
			struct logfile *previous = logfile_root;

			while( previous != NULL && previous->next != logfile )
				previous = previous->next;
			assert( previous != NULL );
			assert( previous->next == logfile );
			previous->next = logfile->next;
		}
		if( logfile->fd >= 0 ) {
			int ret,e;

			ret = close(logfile->fd); logfile->fd = -1;
			if( ret < 0 ) {
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
	assert( logfile != NULL );
	assert( logfile->fd < 0 );

	(void)dirs_make_parent(logfile->filename);
	logfile->fd = open(logfile->filename,
			O_CREAT|O_APPEND|O_LARGEFILE|O_NOCTTY|O_WRONLY,
			0666);
	if( logfile->fd < 0 ) {
		int e = errno;
		fprintf(stderr, "Cannot open/create logfile '%s': %d=%s\n",
				logfile->filename, e, strerror(e));
		return RET_ERRNO(e);
	}
	return RET_OK;
}

static retvalue logfile_write(struct logfile *logfile,struct target *target,const char *name,/*@null@*/const char *version,/*@null@*/const char *oldversion) {
	int ret;
	time_t currenttime;
	struct tm t;

	assert( logfile->fd >= 0 );

	currenttime = time(NULL);
	if( localtime_r(&currenttime, &t) == NULL ) {
		if( version != NULL && oldversion != NULL )
			ret = dprintf(logfile->fd,
"EEEE-EE-EE EE:EE:EE replace %s %s %s %s %s %s %s\n",
				target->codename, target->packagetype,
				target->component, target->architecture,
				name, version, oldversion);
		else if( version != NULL )
			ret = dprintf(logfile->fd,
"EEEE-EE-EE EE:EE:EE add %s %s %s %s %s %s\n",
				target->codename, target->packagetype,
				target->component, target->architecture,
				name, version);
		else
			ret = dprintf(logfile->fd,
"EEEE-EE-EE EE:EE:EE remove %s %s %s %s %s %s\n",
				target->codename, target->packagetype,
				target->component, target->architecture,
				name, oldversion);
	} else if( version != NULL && oldversion != NULL )
		ret = dprintf(logfile->fd,
"%04d-%02d-%02d %02u:%02u:%02u replace %s %s %s %s %s %s %s\n",
			1900+t.tm_year, t.tm_mon+1,
			t.tm_mday, t.tm_hour,
			t.tm_min, t.tm_sec,
			target->codename, target->packagetype,
			target->component, target->architecture,
			name, version, oldversion);
	else if( version != NULL )
		ret = dprintf(logfile->fd,
"%04d-%02d-%02d %02u:%02u:%02u add %s %s %s %s %s %s\n",
			1900+t.tm_year, t.tm_mon+1,
			t.tm_mday, t.tm_hour,
			t.tm_min, t.tm_sec,
			target->codename, target->packagetype,
			target->component, target->architecture,
			name, version);
	else
		ret = dprintf(logfile->fd,
"%04d-%02d-%02d %02u:%02u:%02u remove %s %s %s %s %s %s\n",
			1900+t.tm_year, t.tm_mon+1,
			t.tm_mday, t.tm_hour,
			t.tm_min, t.tm_sec,
			target->codename, target->packagetype,
			target->component, target->architecture,
			name, oldversion);
	if( ret < 0 ) {
		int e = errno;
		fprintf(stderr, "Error writing to log file '%s': %d=%s",
				logfile->filename, e, strerror(e));
		return RET_ERRNO(e);
	}
	return RET_OK;
}

struct notificator {
	char *scriptname;
	/* if one of the following is non-NULL, only call if it matches the package: */
	/*@null@*/char *packagetype;
	/*@null@*/char *component;
	/*@null@*/char *architecture;
	bool_t withcontrol, changesacceptrule;
};

static void notificator_done(struct notificator *n) {
	free(n->scriptname);
	free(n->packagetype);
	free(n->component);
	free(n->architecture);
}

static retvalue notificator_parse(struct notificator *n, const char *confdir, const char *codename, const char *line) {
	const char *p,*q,*s;

	p = line; q = line;
	while( *q != '\0' ) {
		while( *p == ' ' || *p == '\t' )
			p++;
		q = p;
		while( *q != '\0' && *q != ' ' && *q != '\t' )
			q++;
		if( *p == '-' ) {
			char **value_p = NULL;
			p++;
			s = p;
			while( s < q && *s != '=' )
				s++;
			switch( s-p ) {
				case 1:
					if( *p == 'A' )
						value_p = &n->architecture;
					else if( *p == 'C' )
						value_p = &n->component;
					else if( *p == 'T' )
						value_p = &n->packagetype;
					else {
						fprintf(stderr,
"Unknown option in notifiers of '%s': '-%c' (in '%s')\n",
							codename,
							*p,
							line);
						return RET_ERROR;
					}
					break;
				case 5:
					if( memcmp(p, "-type", 5) == 0 )
						value_p = &n->packagetype;
					else {
						fprintf(stderr,
"Unknown option in notifiers of '%s': '%6s' (in '%s')\n",
							codename,
							p-1,
							line);
						return RET_ERROR;
					}
					break;
				case 8:
					if( memcmp(p, "-changes", 8) == 0 )
						n->changesacceptrule = TRUE;
					else {
						fprintf(stderr,
"Unknown option in notifiers of '%s': '%8s' (in '%s')\n",
							codename,
							p-1,
							line);
						return RET_ERROR;
					}
					break;
				case 10:
					if( memcmp(p, "-component", 10) == 0 )
						value_p = &n->component;
					else {
						fprintf(stderr,
"Unknown option in notifiers of '%s': '%11s' (in '%s')\n",
							codename,
							p-1,
							line);
						return RET_ERROR;
					}
					break;
				case 12:
					if( memcmp(p, "-withcontrol", 12) == 0 )
						n->withcontrol = TRUE;
					else {
						fprintf(stderr,
"Unknown option in notifiers of '%s': '%12s' (in '%s')\n",
							codename,
							p-1,
							line);
						return RET_ERROR;
					}
					break;
				case 13:
					if( memcmp(p, "-architecture", 13) == 0 )
						value_p = &n->architecture;
					else {
						fprintf(stderr,
"Unknown option in notifiers of '%s': '%14s' (in '%s')\n",
							codename,
							p-1,
							line);
						return RET_ERROR;
					}
					break;
				default:
					fprintf(stderr,
"Unknown option in notifiers of '%s': '%.*s' (in '%s')\n",
							codename,
							(int)(1+s-p), p-1,
							line);
					return RET_ERROR;
			}
			if( value_p == NULL ) {
				if( s != q ) {
					fprintf(stderr,
"Unexpected '=' in notifiers of '%s' after '%.*s' (in '%s')\n",
							codename,
							(int)(1+s-p), p-1,
							line);
					return RET_ERROR;
				}
				p = q;
				continue;
			}
			/* option expecting string value: */
			if( *s != '=' ) {
				fprintf(stderr,
"Missing '=' in notifiers of '%s' after '%.*s' (in '%s')\n",
						codename, (int)(1+s-p), p-1, line);
				return RET_ERROR;
			}
			if( *value_p != NULL ) {
				fprintf(stderr,
"Double notifier option '%.*s' (in '%s' from '%s')\n",
						(int)(1+s-p), p-1, line, codename);
				return RET_ERROR;
			}
			*value_p = strndup(s+1, q-s-1);
			if( *value_p == NULL )
				return RET_ERROR_OOM;
			p = q;
		} else {
			if( n->changesacceptrule && n->architecture != NULL ) {
				fprintf(stderr,
"--changes and --architecture cannot be combined! (notifier in '%s')\n",
						codename);
				return RET_ERROR;
			}
			if( n->changesacceptrule && n->component != NULL ) {
				fprintf(stderr,
"--changes and --component cannot be combined! (notifier in '%s')\n",
						codename);
				return RET_ERROR;
			}
			if( n->changesacceptrule && n->packagetype != NULL ) {
				fprintf(stderr,
"--changes and --type cannot be combined! (notifier in '%s')\n",
						codename);
				return RET_ERROR;
			}
			if( *q != '\0' ) {
				fprintf(stderr,
"Unexpected data at end of notifier for '%s': '%s'\n",
						codename, q);
				return RET_ERROR;
			}
			if( *p == '/' )
				n->scriptname = strdup(p);
			else
				n->scriptname = calc_dirconcat(confdir, p);
			if( n->scriptname == NULL )
				return RET_ERROR_OOM;
			return RET_OK;
		}
	}
	fprintf(stderr,
"Missing notification script to call in '%s' of '%s'\n",
		line, codename);
	return RET_ERROR;
}

/*@null@*/ static struct notification_process {
	struct notification_process *next;
	char **arguments;
	/* data to send to the process */
	size_t datalen, datasent;
	char *data;
	/* process */
	pid_t child;
	int fd;
} *processes = NULL;

static void notification_process_free(/*@only@*/struct notification_process *p) {
	char **a;

	if( p->fd >= 0 )
		(void)close(p->fd);
	for( a = p->arguments ; *a != NULL ; a++ )
		free(*a);
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
	while( (p=*pp) != NULL ) {
		if( p->child <= 0 ) {
			pp = &p->next;
			continue;
		}

		child = waitpid(p->child, &status, WNOHANG);
		if( child == 0 ) {
			pp = &p->next;
			continue;
		}
		if( child < 0 ) {
			int e = errno;
			fprintf(stderr,
"Error calling waitpid on notification child: %d=%s\n",
					e, strerror(e));
			/* but still handle the failed child: */
		} else if( WIFSIGNALED(status) ) {
			fprintf(stderr,
"Notification process '%s' killed with signal %d!\n",
					p->arguments[0], WTERMSIG(status));
		} else if( !WIFEXITED(status) ) {
			fprintf(stderr,
"Notification process '%s' failed!\n",
					p->arguments[0]);
		} else if( WIFEXITED(status) && WEXITSTATUS(status) != 0 ) {
			fprintf(stderr,
"Notification process '%s' returned with exitcode %d!\n",
					p->arguments[0],
					(int)(WEXITSTATUS(status)));
		}
		if( p->fd >= 0 ) {
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

static void feedchildren(bool_t wait) {
	struct notification_process *p;
	fd_set w;
	int ret;
	int number = 0;
	struct timeval tv = {0,0};

	FD_ZERO(&w);
	for( p = processes; p!= NULL ; p = p->next ) {
		if( p->child > 0 && p->fd >= 0 && p->datasent < p->datalen ) {
			FD_SET(p->fd, &w);
			if( p->fd >= number )
				number = p->fd + 1;
		}
	}
	if( number == 0 )
		return;
	ret = select(number, NULL, &w, NULL, wait?NULL:&tv);
	if( ret < 0 ) {
		// TODO...
		return;
	}
	for( p = processes; p != NULL ; p = p->next ) {
		if( p->child > 0 && p->fd >= 0 && FD_ISSET(p->fd, &w) ) {
			size_t tosent = p->datalen - p->datasent;
			ssize_t sent;

			if( tosent > (size_t)512 )
				tosent = 512;
			sent = write(p->fd, p->data+p->datasent, 512);
			if( sent < 0 ) {
				int e = errno;
				fprintf(stderr,
"Error '%s' while sending data to '%s', sending SIGABRT to it!\n",
						strerror(e),
						p->arguments[0]);
				kill(p->child, SIGABRT);
			}
			p->datasent += sent;
			if( p->datasent >= p->datalen ) {
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
	while( p != NULL && p->child != 0 ) {
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
	while( p != NULL && p->child != 0 )
		p = p->next;
	if( p == NULL )
		return RET_NOTHING;
	if( p->datalen > 0 ) {
		ret = pipe(filedes);
		if( ret < 0 ) {
			int e = errno;
			fprintf(stderr, "Error creating pipe: %d=%s!\n", e, strerror(e));
			return RET_ERRNO(e);
		}
		p->fd = filedes[1];
	} else {
		p->fd = -1;
	}
	child = fork();
	if( child == 0 ) {
		int maxopen;

		if( p->datalen > 0 ) {
			dup2(filedes[0], 0);
			if( filedes[0] != 0)
				(void)close(filedes[0]);
		}
		/* Try to close all open fd but 0,1,2 */
		maxopen = sysconf(_SC_OPEN_MAX);
		if( maxopen > 0 ) {
			int fd;
			for( fd = 3 ; fd < maxopen ; fd++ )
				(void)close(fd);
		} else {
			/* close at least the ones definitly causing problems*/
			const struct notification_process *q;
			for( q = processes; q != NULL ; q = q->next ) {
				if( q != p && q->fd >= 0 )
					(void)close(q->fd);
			}
		}
		(void)execv(p->arguments[0], p->arguments);
		fprintf(stderr, "Error executing '%s': %s\n", p->arguments[0],
				strerror(errno));
		_exit(255);
	}
	if( p->datalen > 0 )
		close(filedes[0]);
	if( child < 0 ) {
		int e = errno;
		fprintf(stderr, "Error forking: %d=%s!\n", e, strerror(e));
		if( p->fd >= 0 ) {
			(void)close(p->fd);
			p->fd = -1;
		}
		return RET_ERRNO(e);
	}
	p->child = child;
	if( p->datalen > 0 ) {
		struct pollfd polldata;
		ssize_t written;

		polldata.fd = p->fd;
		polldata.events = POLLOUT;
		while( poll(&polldata, 1, 0) > 0 ) {
			if( (polldata.revents & POLLNVAL) != 0 ) {
				p->fd = -1;
				return RET_ERROR;
			}
			if( (polldata.revents & POLLHUP) != 0 ) {
				close(p->fd);
				p->fd = -1;
				return RET_OK;
			}
			if( (polldata.revents & POLLOUT) != 0 ) {
				size_t towrite =  p->datalen - p->datasent;
				if( towrite > (size_t)512 )
					towrite = 512;
				written = write(p->fd,
						p->data + p->datasent,
						towrite);
				if( written < 0 ) {
					int e = errno;
					fprintf(stderr,
"Error '%s' while sending data to '%s', sending SIGABRT to it!\n",
							strerror(e),
							p->arguments[0]);
					kill(p->child, SIGABRT);
					return RET_ERRNO(e);
				}
				p->datasent += written;
				if( p->datasent >= p->datalen ) {
					int ret;
					free(p->data);
					p->data = NULL;
					ret = close(p->fd);
					p->fd = -1;
					if( ret != 0 )
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

static retvalue notificator_enqueuechanges(struct notificator *n,const char *codename,const char *name,const char *version,const char *changeschunk,const char *safefilename,/*@null@*/const char *filekey) {
	size_t count,i,j;
	char **arguments;
	struct notification_process *p;

	catchchildren();
	feedchildren(FALSE);
	if( !n->changesacceptrule )
		return RET_NOTHING;
	count = 6; /* script "accepted" codename name version safename */
	if( filekey != NULL )
		count++;
	arguments = calloc(count+1, sizeof(char*));
	if( arguments == NULL )
		return RET_ERROR_OOM;
	i = 0;
	arguments[i++] = strdup(n->scriptname);
	arguments[i++] = strdup("accepted");
	arguments[i++] = strdup(codename);
	arguments[i++] = strdup(name);
	arguments[i++] = strdup(version);
	arguments[i++] = strdup(safefilename);
	if( filekey != NULL )
		arguments[i++] = strdup(filekey);
	assert( i == count );
	arguments[i] = NULL;
	for( i = 0 ; i < count ; i++ )
		if( arguments[i] == NULL ) {
			for( j = 0 ; j < count ; j++ )
				free(arguments[j]);
			free(arguments);
			return RET_ERROR_OOM;
		}
	if( processes == NULL ) {
		p = malloc(sizeof(struct notification_process));
		processes = p;
	} else {
		p = processes;
		while( p->next != NULL )
			p = p->next;
		p->next = malloc(sizeof(struct notification_process));
		p = p->next;
	}
	if( p == NULL ) {
		for( j = 0 ; j < count ; j++ )
			free(arguments[j]);
		free(arguments);
		return RET_ERROR_OOM;
	}
	p->arguments = arguments;
	p->next = NULL;
	p->child = 0;
	p->fd = -1;
	p->datalen = 0;
	p->datasent = 0;
	p->data = NULL;
	// TODO: implement --withcontrol
	if( runningchildren() < 1 )
		startchild();
	return RET_OK;
}

static retvalue notificator_enqueue(struct notificator *n,struct target *target,const char *name,/*@null@*/const char *version,/*@null@*/const char *oldversion,/*@null@*/const char *control,/*@null@*/const char *oldcontrol,/*@null@*/const struct strlist *filekeys,/*@null@*/const struct strlist *oldfilekeys,bool_t renotification) {
	size_t count,i,j;
	char **arguments;
	const char *action = NULL;
	struct notification_process *p;

	catchchildren();
	feedchildren(FALSE);
	if( n->changesacceptrule )
		return RET_NOTHING;
	// some day, some atom handling for those would be nice
	if( n->architecture != NULL &&
			strcmp(n->architecture,target->architecture) != 0 ) {
		if( runningchildren() < 1 )
			startchild();
		return RET_NOTHING;
	}
	if( n->component != NULL &&
			strcmp(n->component,target->component) != 0 ) {
		if( runningchildren() < 1 )
			startchild();
		return RET_NOTHING;
	}
	if( n->packagetype != NULL &&
			strcmp(n->packagetype,target->packagetype) != 0 ) {
		if( runningchildren() < 1 )
			startchild();
		return RET_NOTHING;
	}
	count = 7; /* script action codename type component architecture */
	if( version != NULL ) {
		action = "add";
		count += 2; /* version and filekeylist marker */
		if( filekeys != NULL )
			count += filekeys->count;
	}
	if( oldversion != NULL ) {
		assert( !renotification );

		if( action == NULL )
			action = "remove";
		else
			action = "replace";

		count += 2; /* version and filekeylist marker */
		if( oldfilekeys != NULL )
			count += oldfilekeys->count;
	}
	assert( action != NULL );
	if( renotification )
		action = "info";
	arguments = calloc(count+1, sizeof(char*));
	if( arguments == NULL )
		return RET_ERROR_OOM;
	i = 0;
	arguments[i++] = strdup(n->scriptname);
	arguments[i++] = strdup(action);
	arguments[i++] = strdup(target->codename);
	arguments[i++] = strdup(target->packagetype);
	arguments[i++] = strdup(target->component);
	arguments[i++] = strdup(target->architecture);
	arguments[i++] = strdup(name);
	if( version != NULL )
		arguments[i++] = strdup(version);
	if( oldversion != NULL )
		arguments[i++] = strdup(oldversion);
	if( version != NULL ) {
		arguments[i++] = strdup("--");
		if( filekeys != NULL )
			for( j = 0 ; j < filekeys->count ; j++ )
				arguments[i++] = strdup(filekeys->values[j]);
	}
	if( oldversion != NULL ) {
		arguments[i++] = strdup("--");
		if( oldfilekeys != NULL )
			for( j = 0 ; j < oldfilekeys->count ; j++ )
				arguments[i++] = strdup(oldfilekeys->values[j]);
	}
	assert( i == count );
	arguments[i] = NULL;
	for( i = 0 ; i < count ; i++ )
		if( arguments[i] == NULL ) {
			for( j = 0 ; j < count ; j++ )
				free(arguments[j]);
			free(arguments);
			return RET_ERROR_OOM;
		}
	if( processes == NULL ) {
		p = malloc(sizeof(struct notification_process));
		processes = p;
	} else {
		p = processes;
		while( p->next != NULL )
			p = p->next;
		p->next = malloc(sizeof(struct notification_process));
		p = p->next;
	}
	if( p == NULL ) {
		for( j = 0 ; j < count ; j++ )
			free(arguments[j]);
		free(arguments);
		return RET_ERROR_OOM;
	}
	p->arguments = arguments;
	p->next = NULL;
	p->child = 0;
	p->fd = -1;
	p->datalen = 0;
	p->datasent = 0;
	p->data = NULL;
	// TODO: implement --withcontrol
	if( runningchildren() < 1 )
		startchild();
	return RET_OK;
}

void logger_wait(void) {
	while( processes != NULL ) {
		catchchildren();
		if( interrupted() )
			break;
		feedchildren(TRUE);
		// TODO: add option to start multiple at the same time
		if( runningchildren() < 1 )
			startchild();
		else {
			struct timeval tv = { 0, 100 };
			select(0, NULL, NULL, NULL, &tv);
		}
	}
}

void logger_warn_waiting(void) {
	struct notification_process *p;

	if( processes != NULL ) {
		fputs(
"WARNING: some notificator hooks were not run!\n"
"(most likely due to receiving an interruption request)\n"
"You will either have to run them by hand or run rerunnotifiers if\n"
"you want the information they get to not be out of sync.\n"
"Missed calls are:\n", stderr);
		for( p = processes ; p != NULL ; p = p->next ) {
			char **c = p->arguments;
			if( c == NULL )
				continue;
			while( *c != NULL ) {
				fputc('"', stderr);
				fputs(*c, stderr);
				fputc('"', stderr);
				c++;
				if( *c != NULL )
					fputc(' ', stderr);
			}
			fputc('\n', stderr);
		}
	}
}

struct logger {
	/*@dependent@*/struct logfile *logfile;
	size_t notificator_count;
	struct notificator *notificators;
};

void logger_free(struct logger *logger) {
	if( logger == NULL )
		return;

	if( logger->logfile != NULL )
		logfile_dereference(logger->logfile);
	if( logger->notificators != NULL ) {
		int i;

		for( i = 0 ; i < logger->notificator_count ; i++ )
			notificator_done(&logger->notificators[i]);
		free(logger->notificators);
	}

	free(logger);
}

retvalue logger_init(const char *confdir,const char *logdir,const char *codename,const char *option,const struct strlist *notificators,struct logger **logger_p) {
	struct logger *n;
	retvalue r;

	if( (option == NULL || *option == '\0')
		&& (notificators == NULL || notificators->count == 0) ) {
		*logger_p = NULL;
		return RET_NOTHING;
	}
	n = malloc(sizeof(struct logger));
	if( n == NULL )
		return RET_ERROR_OOM;
	if( option != NULL && *option != '\0' ) {
		r = logfile_reference(logdir, option, &n->logfile);
		if( RET_WAS_ERROR(r) ) {
			free(n);
			return r;
		}
	} else
		n->logfile = NULL;
	if( notificators != NULL && notificators->count > 0 ) {
		int i;
		n->notificator_count = notificators->count;
		n->notificators = calloc(n->notificator_count, sizeof(struct notificator));
		if( n->notificators == NULL ) {
			if( n->logfile != NULL )
				logfile_dereference(n->logfile);
			free(n);
			return RET_ERROR_OOM;
		}
		for( i = 0 ; i < notificators->count ; i++ ) {
			r = notificator_parse(&n->notificators[i], confdir, codename,
					notificators->values[i]);
			if( RET_WAS_ERROR(r) ) {
				/* a bit ugly: also free the just failed item here */
				while( i >= 0 ) {
					notificator_done(&n->notificators[i]);
					i--;
				}

				if( n->logfile != NULL )
					logfile_dereference(n->logfile);
				free(n->notificators);
				free(n);
				return r;
			}
		}
	} else {
		n->notificators = NULL;
		n->notificator_count = 0;
	}
	*logger_p = n;
	return RET_OK;
}

retvalue logger_prepare(struct logger *logger) {
	retvalue r;

	if( logger->logfile == NULL )
		return RET_NOTHING;

	if( logger->logfile != NULL && logger->logfile->fd < 0 ) {
		r = logfile_open(logger->logfile);
	} else
		r = RET_OK;
	return r;
}
bool_t logger_isprepared(/*@null@*/const struct logger *logger) {
	if( logger == NULL )
		return TRUE;
	if( logger->logfile != NULL && logger->logfile->fd < 0 )
		return FALSE;
	return TRUE;
}

void logger_log(struct logger *log,struct target *target,const char *name,const char *version,const char *oldversion,const char *control,const char *oldcontrol,const struct strlist *filekeys, const struct strlist *oldfilekeys) {
	size_t i;

	assert( name != NULL );
	assert( control != NULL || oldcontrol != NULL );

	assert( version != NULL || control == NULL );
	/* so that a replacement can be detected by existance of oldversion */
	if( oldcontrol != NULL && oldversion == NULL )
		oldversion = "#unparseable#";

	assert( version != NULL || oldversion != NULL );

	if( log->logfile != NULL )
		logfile_write(log->logfile, target, name, version, oldversion);
	for( i = 0 ; i < log->notificator_count ; i++ ) {
		notificator_enqueue(&log->notificators[i], target,
				name, version, oldversion,
				control, oldcontrol,
				filekeys, oldfilekeys, FALSE);
	}
}

void logger_logchanges(struct logger *log,const char *codename,const char *name,const char *version,const char *data,const char *safefilename,const char *changesfilekey) {
	size_t i;

	assert( name != NULL );
	assert( version != NULL );

	for( i = 0 ; i < log->notificator_count ; i++ ) {
		notificator_enqueuechanges(&log->notificators[i], codename,
				name, version, data, safefilename, changesfilekey);
	}
}

bool_t logger_rerun_needs_target(const struct logger *logger,const struct target *target) {
	int i;
	struct notificator *n;

	for( i = 0 ; i < logger->notificator_count ; i++ ) {
		n = &logger->notificators[i];

		if( n->architecture != NULL &&
				strcmp(n->architecture,target->architecture) != 0 ) {
			continue;
		}
		if( n->component != NULL &&
				strcmp(n->component,target->component) != 0 ) {
			continue;
		}
		if( n->packagetype != NULL &&
				strcmp(n->packagetype,target->packagetype) != 0 ) {
			continue;
		}
		return TRUE;
	}
	return FALSE;
}

retvalue logger_reruninfo(struct logger *logger,struct target *target,const char *name,const char *version,const char *control,/*@null@*/const struct strlist *filekeys) {
	retvalue result,r;
	size_t i;

	assert( name != NULL );
	assert( version != NULL );
	assert( control != NULL );

	result = RET_NOTHING;

	for( i = 0 ; i < logger->notificator_count ; i++ ) {
		r = notificator_enqueue(&logger->notificators[i], target,
				name, version, NULL,
				control, NULL,
				filekeys, NULL, TRUE);
		RET_UPDATE(result,r);
	}
	return result;
}
