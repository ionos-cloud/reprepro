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
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <malloc.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "chunks.h"
#include "packages.h"
#include "binaries.h"
#include "sources.h"
#include "names.h"
#include "target.h"

extern int verbose;

static retvalue target_initialize(
	const char *codename,const char *component,const char *architecture,
	get_name getname,get_version getversion,get_installdata getinstalldata,
	target *d) {

	target t;

	t = calloc(1,sizeof(struct s_target));
	if( t == NULL )
		return RET_ERROR_OOM;
	t->codename = strdup(codename);
	t->component = strdup(component);
	t->architecture = strdup(architecture);
	t->identifier = calc_identifier(codename,component,architecture);
	if( !t->codename|| !t->component|| !t->architecture|| !t->identifier) {
		target_done(t);
		return RET_ERROR_OOM;
	}
	t->getname = getname;
	t->getversion = getversion;
	t->getinstalldata = getinstalldata;
	*d = t;
	return RET_OK;
}

retvalue target_initialize_binary(const char *codename,const char *component,const char *architecture,target *target) {
	return target_initialize(codename,component,architecture,binaries_getname,binaries_getversion,binaries_getinstalldata,target);
}

retvalue target_initialize_source(const char *codename,const char *component,target *target) {
	return target_initialize(codename,component,"source",sources_getname,sources_getversion,sources_getinstalldata,target);
}


void target_done(target target) {
	if( target == NULL )
		return;
	free(target->codename);
	free(target->component);
	free(target->architecture);
	free(target->identifier);
	free(target);
}

