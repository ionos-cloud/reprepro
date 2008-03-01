/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2008 Bernhard R. Link
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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>
#include "error.h"
#include "names.h"
#include "checksums.h"
#include "changes.h"

retvalue changes_parsefileline(const char *fileline, /*@out@*/filetype *result_type, /*@out@*/char **result_basename, /*@out@*/struct checksums **result_checksums, /*@out@*/char **result_section, /*@out@*/char **result_priority, /*@out@*/char **result_architecture, /*@out@*/char **result_name) {

	const char *p,*md5start,*md5end;
	const char *sizestart,*sizeend;
	const char *sectionstart,*sectionend;
	const char *priostart,*prioend;
	const char *filestart,*nameend,*fileend;
	const char *archstart,*archend;
	const char *versionstart,*typestart;
	filetype type;
	char *section, *priority, *basename, *architecture, *name;
	retvalue r;

	p = fileline;
	while( *p !='\0' && xisspace(*p) )
		p++;
	md5start = p;
	while( (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') )
		p++;
	if( *p == '\0' ) {
		fprintf(stderr,"Missing md5sum in '%s'!\n", fileline);
		return RET_ERROR;
	}
	if( !xisspace(*p) ) {
		fprintf(stderr,"Malformed md5 hash in '%s'!\n", fileline);
		return RET_ERROR;
	}
	md5end = p;
	while( *p !='\0' && xisspace(*p) )
		p++;
	sizestart = p;
	while( *p >= '0' && *p <= '9' )
		p++;
	if( *p == '\0' ) {
		fprintf(stderr,"Missing size (second argument) in '%s'!\n", fileline);
		return RET_ERROR;
	}
	if( !xisspace(*p) ) {
		fprintf(stderr,"Malformed size (second argument) in '%s'!\n", fileline);
		return RET_ERROR;
	}
	sizeend = p;
	while( *p !='\0' && xisspace(*p) )
		p++;
	sectionstart = p;
	while( *p !='\0' && !xisspace(*p) )
		p++;
	sectionend = p;
	while( *p !='\0' && xisspace(*p) )
		p++;
	priostart = p;
	while( *p !='\0' && !xisspace(*p) )
		p++;
	prioend = p;
	while( *p !='\0' && xisspace(*p) )
		p++;
	filestart = p;
	while( *p !='\0' && !xisspace(*p) )
		p++;
	fileend = p;
	while( *p !='\0' && xisspace(*p) )
		p++;
	if( *p != '\0' ) {
		fprintf(stderr,"Unexpected sixth argument in '%s'!\n",fileline);
		return RET_ERROR;
	}
	if( *md5start == '\0' || *sizestart == '\0' || *sectionstart == '\0'
			|| *priostart == '\0' || *filestart == '\0' ) {
		fprintf(stderr,"Not five arguments in '%s'!\n",fileline);
		return RET_ERROR;
	}

	p = filestart;
	while( *p != '\0' && *p != '_' && !xisspace(*p) )
		p++;
	if( *p != '_' ) {
		if( *p == '\0' )
			fprintf(stderr,"No underscore in filename in '%s'!\n",fileline);
		else
			fprintf(stderr,"Unexpected character '%c' in filename in '%s'!\n",*p,fileline);
		return RET_ERROR;
	}
	nameend = p;
	p++;
	versionstart = p;
	// We cannot say where the version ends and the filename starts,
	// but as the packagetypes would be valid part of the version, too,
	// this check gets the broken things.
	names_overversion(&p, true);
	if( *p != '\0' && *p != '_' ) {
		fprintf(stderr,"Unexpected character '%c' in filename within '%s'!\n",*p,fileline);
		return RET_ERROR;
	}
	if( *p == '_' ) {
		/* Things having a underscole will have an architecture
		 * and be either .deb or .udeb */
		p++;
		archstart = p;
		while( *p !='\0' && *p != '.' )
			p++;
		if( *p != '.' ) {
			fprintf(stderr,"Expect something of the form name_version_arch.[u]deb but got '%s'!\n",filestart);
			return RET_ERROR;
		}
		archend = p;
		p++;
		typestart = p;
		while( *p !='\0' && !xisspace(*p) )
			p++;
		if( p-typestart == 3 && strncmp(typestart,"deb",3) == 0 )
			type = fe_DEB;
		else if( p-typestart == 4 && strncmp(typestart,"udeb",4) == 0 )
			type = fe_UDEB;
		else {
			fprintf(stderr,"'%s' looks neighter like .deb nor like .udeb!\n",filestart);
			return RET_ERROR;
		}
		if( strncmp(archstart,"source",6) == 0 ) {
			fprintf(stderr,"How can a .[u]deb be architecture 'source'?('%s')\n",filestart);
			return RET_ERROR;
		}
	} else {
		/* this looks like some source-package, we will have
		 * to look for the packagetype ourself... */
		while( *p !='\0' && !xisspace(*p) ) {
			p++;
		}
		if( p-versionstart > 12 && strncmp(p-12,".orig.tar.gz",12) == 0 )
			type = fe_ORIG;
		else if( p-versionstart > 7 && strncmp(p-7,".tar.gz",7) == 0 )
			type = fe_TAR;
		else if( p-versionstart > 8 && strncmp(p-8,".diff.gz",8) == 0 )
			type = fe_DIFF;
		else if( p-versionstart > 4 && strncmp(p-4,".dsc",4) == 0 )
			type = fe_DSC;
		else if( p-versionstart > 13 && strncmp(p-13,".orig.tar.bz2",13) == 0 )
			type = fe_ORIG;
		else if( p-versionstart > 8 && strncmp(p-8,".tar.bz2",8) == 0 )
			type = fe_TAR;
		else if( p-versionstart > 9 && strncmp(p-9,".diff.bz2",9) == 0 )
			type = fe_DIFF;
		else if( p-versionstart > 14 && strncmp(p-14,".orig.tar.lzma",14) == 0 )
			type = fe_ORIG;
		else if( p-versionstart > 9 && strncmp(p-9,".tar.lzma",9) == 0 )
			type = fe_TAR;
		else if( p-versionstart > 10 && strncmp(p-10,".diff.lzma",10) == 0 )
			type = fe_DIFF;
		else {
			type = fe_UNKNOWN;
			fprintf(stderr,"Unknown filetype: '%s', assuming to be source format...\n",fileline);
		}
		archstart = "source";
		archend = archstart + 6;
	}
	section = strndup(sectionstart,sectionend-sectionstart);
	priority = strndup(priostart,prioend-priostart);
	basename = strndup(filestart,fileend-filestart);
	architecture = strndup(archstart,archend-archstart);
	name = strndup(filestart,nameend-filestart);
	if( section == NULL || priority == NULL ||
	    basename == NULL || architecture == NULL || name == NULL ) {
		free(section); free(priority);
		free(basename); free(architecture); free(name);
		return RET_ERROR_OOM;
	}
	r = checksums_set(result_checksums, md5start, md5end - md5start,
			sizestart, sizeend - sizestart);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) ) {
		free(section); free(priority);
		free(basename); free(architecture); free(name);
		return r;
	}
	*result_section = section;
	*result_priority = priority;
	*result_basename = basename;
	*result_architecture = architecture;
	*result_name = name;
	*result_type = type;
	return RET_OK;
}
