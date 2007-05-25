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
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "error.h"
#include "freespace.h"

struct device {
	/*@null@*/struct device *next;
	/* stat(2)'s st_dev number identifying this device */
	dev_t id;
	/* some directory in this filesystem */
	char *somepath;
	/* size of one block on this device according to statvfs(2) */
	unsigned long blocksize;
	/* blocks available for us */
	fsblkcnt_t available;
	/* blocks already known to be needed on that device */
	fsblkcnt_t needed;
	/* calculated block to keep free */
	fsblkcnt_t reserved;
};

struct devices {
	/*@null@*/struct device *root;
};

void space_free(struct devices *devices) {
	struct device *d;

	if( devices == NULL )
		return;

	while( (d = devices->root) != NULL ) {
		devices->root = d->next;

		free(d->somepath);
		free(d);
	}
	free(devices);
}

static retvalue device_find_or_create(struct devices *devices, dev_t id, const char *dirname, /*@out@*/struct device **result) {
	struct device *d;
	struct statvfs s;
	int ret;

	d = devices->root;

	while( d != NULL && d->id != id )
		d = d->next;

	if( d != NULL ) {
		*result = d;
		return RET_OK;
	}

	ret = statvfs(dirname, &s);
	if( ret != 0 ) {
		int e = errno;
		fprintf(stderr,
"Error judging free space for the fileystem '%s' belongs to: %d=%s\n"
"(Take a look at --spacecheck in the manpage on how to modify checking.)\n",
					dirname, e, strerror(e));
		return RET_ERRNO(e);
	}

	d = malloc(sizeof(struct device));
	if( d == NULL )
		return RET_ERROR_OOM;
	d->next = devices->root;
	d->id = id;
	d->somepath = strdup(dirname);
	if( d->somepath == NULL ) {
		free(d);
		return RET_ERROR_OOM;
	}
	d->blocksize = s.f_bsize;
	/* use bfree when being root? but why run as root? */
	d->available = s.f_bavail;
	d->needed = 0;
	/* always keep at least one megabyte spare */
	d->reserved = (1024*1024)/d->blocksize+1;
	devices->root = d;
	*result = d;
	return RET_OK;
}

retvalue space_prepare(const char *dbdir,struct devices **devices) {
	struct devices *n;
	struct device *d;
	struct stat s;
	int ret;
	retvalue r;

	n = malloc(sizeof(struct devices));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->root = NULL;

	ret = stat(dbdir ,&s);
	if( ret != 0 ) {
		int e = errno;
		fprintf(stderr, "Error stat'ing %s: %d=%s\n", dbdir,
						e, strerror(e));
		free(n);
		return RET_ERRNO(e);
	}
	r = device_find_or_create(n, s.st_dev, dbdir, &d);
	if( RET_WAS_ERROR(r) ) {
		space_free(n);
		return r;
	}
	// TODO: what is a reasonable number here?
	d->reserved += (100*1024*1024)/d->blocksize+1;
	*devices = n;
	return RET_OK;
}

retvalue space_needed(struct devices *devices,const char *filename,const char *md5sum) {
	size_t l = strlen(filename);
	char buffer[l+1];
	struct stat s;
	struct device *device;
	int ret;
	retvalue r;
	fsblkcnt_t blocks;
	off_t filesize;
	const char *p;

	if( devices == NULL )
		return RET_NOTHING;

	while( l > 0 && filename[l-1] != '/' )
		l--;
	assert( l > 0 );
	memcpy(buffer, filename, l);
	buffer[l] = '\0';

	ret = stat(buffer ,&s);
	if( ret != 0 ) {
		int e = errno;
		fprintf(stderr, "Error stat'ing %s: %d=%s\n", filename,
						e, strerror(e));
		return RET_ERRNO(e);
	}
	r = device_find_or_create(devices, s.st_dev, buffer, &device);
	if( RET_WAS_ERROR(r) )
		return r;
	p = md5sum;
	/* over md5 part to the length part */
	while( *p != ' ' && *p != '\0' )
		p++;
	if( *p != ' ' ) {
		fprintf(stderr, "Cannot extract filesize from '%s'\n", md5sum);
		return RET_ERROR;
	}
	while( *p == ' ' )
		p++;
	filesize = 0;
	while( *p <= '9' && *p >= '0' ) {
		filesize = filesize*10 + (*p-'0');
		p++;
	}
	if( *p != '\0' ) {
		fprintf(stderr, "Cannot extract filesize from '%s'\n", md5sum);
		return RET_ERROR;
	}
	blocks = (filesize + device->blocksize - 1) / device->blocksize;
	device->needed += blocks;

	return RET_OK;
}

retvalue space_check(struct devices *devices) {
	struct device *device;
	struct statvfs s;
	int ret;
	retvalue result = RET_OK;


	if( devices == NULL )
		return RET_NOTHING;

	for( device = devices->root ; device != NULL ; device = device->next ) {
		/* recalculate free space, as created directories
		 * and other stuff might have changed it */

		ret = statvfs(device->somepath, &s);
		if( ret != 0 ) {
			int e = errno;
			fprintf(stderr,
"Error judging free space for the fileystem '%s' belongs to: %d=%s\n"
"(As this worked before in this run, something must have changed strangely)\n",
					device->somepath,
					e, strerror(e));
			return RET_ERRNO(e);
		}
		if( device->blocksize != s.f_bsize ) {
			fprintf(stderr,
"The blocksize of the filesystem belonging to '%s' has changed.\n"
"Either something was mounted or unmounted while reprepro was running,\n"
"or some symlinks were changed. Aborting as utterly confused.\n",
					device->somepath);
		}
		device->available = s.f_bavail;
		if( device->needed >= device->available ) {
			fprintf(stderr,
"NOT ENOUGH FREE SPACE on filesystem 0x%lx (the filesystem '%s' is on)\n"
"available blocks %llu, needed blocks %llu, block size is %llu.\n",
				(unsigned long)device->id, device->somepath,
				(unsigned long long)device->available,
				(unsigned long long)device->needed,
				(unsigned long long)device->blocksize);
			result = RET_ERROR;
		} else if( device->needed >= device->available+device->reserved ) {
			fprintf(stderr,
"NOT ENOUGH FREE SPACE on filesystem 0x%lx (the filesystem '%s' is on)\n"
"available blocks %llu, needed blocks %llu (+%llu savety margin), block size is %llu.\n",
				(unsigned long)device->id, device->somepath,
				(unsigned long long)device->available,
				(unsigned long long)device->needed,
				(unsigned long long)device->reserved,
				(unsigned long long)device->blocksize);
			result = RET_ERROR;
		}
	}
	return result;
}
