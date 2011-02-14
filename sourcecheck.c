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

#include <sys/types.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "error.h"
#include "distribution.h"
#include "trackingt.h"
#include "sourcecheck.h"

/* This is / will be the implementation of the
 * 	removeunusedsources (to be implemented...)
 *	unusedsources
 *	withoutsource
 * commands.
 *
 * Currently those only work with tracking enabled, but
 * are in this file as the implementation without tracking
 * will need similar infrastructure */

static retvalue listunusedsources(struct distribution *d, const struct trackedpackage *pkg) {
	bool hasbinary = false, hassource = false;
	int i;

	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		if( pkg->refcounts[i] == 0 )
			continue;
		if( pkg->filetypes[i] == 's' )
			hassource = true;
		if( pkg->filetypes[i] == 'b' )
			hasbinary = true;
		if( pkg->filetypes[i] == 'a' )
			hasbinary = true;
	}
	if( hassource && ! hasbinary ) {
		printf("%s %s %s\n", d->codename, pkg->sourcename, pkg->sourceversion);
		return RET_OK;
	}
	return RET_NOTHING;
}

retvalue unusedsources(struct database *database, struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;
		if( d->tracking == dt_NONE ) {
			fprintf(stderr, "Warning: Tracking not enabled for '%s' and unusedsources yet only possible with tracking information.\n",
					d->codename);
			continue;
		}

		r = tracking_foreach_ro(database, d, listunusedsources);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return result;
}

static retvalue listsourcemissing(struct distribution *d, const struct trackedpackage *pkg) {
	bool hasbinary = false, hassource = false;
	int i;

	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		if( pkg->refcounts[i] == 0 )
			continue;
		if( pkg->filetypes[i] == 's' )
			hassource = true;
		if( pkg->filetypes[i] == 'b' )
			hasbinary = true;
		if( pkg->filetypes[i] == 'a' )
			hasbinary = true;
	}
	if( hasbinary && ! hassource ) {
		for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
			if( pkg->refcounts[i] == 0 )
				continue;
			if( pkg->filetypes[i] != 'b' && pkg->filetypes[i] != 'a' )
				continue;
			printf("%s %s %s %s\n", d->codename, pkg->sourcename, pkg->sourceversion, pkg->filekeys.values[i]);
		}
		return RET_OK;
	}
	return RET_NOTHING;
}

retvalue sourcemissing(struct database *database, struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;
		if( d->tracking == dt_NONE ) {
			fprintf(stderr, "Warning: Tracking not enabled for '%s' and unusedsources yet only possible with tracking information.\n",
					d->codename);
			continue;
		}

		r = tracking_foreach_ro(database, d, listsourcemissing);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return result;
}

static retvalue listcruft(struct distribution *d, const struct trackedpackage *pkg) {
	bool hasbinary = false, hassource = false;
	int i;

	for( i = 0 ; i < pkg->filekeys.count ; i++ ) {
		if( pkg->refcounts[i] == 0 )
			continue;
		if( pkg->filetypes[i] == 's' )
			hassource = true;
		if( pkg->filetypes[i] == 'b' )
			hasbinary = true;
		if( pkg->filetypes[i] == 'a' )
			hasbinary = true;
	}
	if( hasbinary && ! hassource ) {
		printf("binaries-without-source %s %s %s\n", d->codename, pkg->sourcename, pkg->sourceversion);
		return RET_OK;
	} else if( hassource && ! hasbinary ) {
		printf("source-without-binaries %s %s %s\n", d->codename, pkg->sourcename, pkg->sourceversion);
		return RET_OK;
	}
	return RET_NOTHING;
}

retvalue reportcruft(struct database *database, struct distribution *alldistributions) {
	struct distribution *d;
	retvalue result = RET_NOTHING, r;

	for( d = alldistributions ; d != NULL ; d = d->next ) {
		if( !d->selected )
			continue;
		if( d->tracking == dt_NONE ) {
			fprintf(stderr, "Warning: Tracking not enabled for '%s' and unusedsources yet only possible with tracking information.\n",
					d->codename);
			continue;
		}

		r = tracking_foreach_ro(database, d, listcruft);
		RET_UPDATE(result, r);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return result;
}
