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
#include <sys/types.h>
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

struct logfile {
	struct logfile *next;
	char *filename;
	size_t refcount;
	int fd;
} *logfile_root = NULL;

static retvalue logfile_reference(const char *logdir,const char *filename,struct logfile **logfile) {
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

static retvalue logfile_write(struct logfile *logfile,struct target *target,const char *name,const char *version,const char *oldversion) {
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

struct logger {
	struct logfile *logfile;
};

void logger_free(struct logger *logger) {
	if( logger == NULL )
		return;

	if( logger->logfile != NULL )
		logfile_dereference(logger->logfile);
	free(logger);
}

retvalue logger_init(const char *confdir,const char *logdir,const char *option,struct logger **logger_p) {
	struct logger *n;
	retvalue r;

	if( option == NULL || *option == '\0' ) {
		*logger_p = NULL;
		return RET_NOTHING;
	}
	n = malloc(sizeof(struct logger));
	if( n == NULL )
		return RET_ERROR_OOM;
	r = logfile_reference(logdir, option, &n->logfile);
	if( RET_WAS_ERROR(r) ) {
		free(n);
		return r;
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

	assert( name != NULL );
	assert( control != NULL || oldcontrol != NULL );

	/* so that logfile_write can detect a replacement by the oldversion */
	if( oldcontrol != NULL && oldversion == NULL )
		oldversion = "#unparseable#";

	assert( version != NULL || oldversion != NULL );

	if( log->logfile != NULL )
		logfile_write(log->logfile, target, name, version, oldversion);
}

