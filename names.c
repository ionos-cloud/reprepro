/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2003 Bernhard R. Link
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

#include <assert.h>
#include <stdio.h>
#include "mprintf.h"
#include "names.h"

char *calc_identifier(const char *codename,const char *component,const char *architecture) {
	return mprintf("%s-%s-%s",codename,component,architecture);
}

char *calc_addsuffix(const char *str1,const char *str2) {
	return mprintf("%s.%s",str1,str2);
}

char *calc_dirconcat(const char *str1,const char *str2) {
	return mprintf("%s/%s",str1,str2);
}

char *calc_srcfilekey(const char *sourcedir,const char *filename){
	return calc_dirconcat(sourcedir,filename);
}

char *calc_fullfilename(const char *mirrordir,const char *filekey){
	return calc_dirconcat(mirrordir,filekey);
}

char *calc_fullsrcfilename(const char *mirrordir,const char *directory,const char *filename){
	return mprintf("%s/%s/%s",mirrordir,directory,filename);
}

char *calc_sourcedir(const char *component,const char *sourcename) {

	assert( *sourcename != '\0' );

	if( sourcename[0] == 'l' && sourcename[1] == 'i' && sourcename[2] == 'b' && sourcename[3] != '\0' )

		return mprintf("pool/%s/lib%c/%s",component,sourcename[3],sourcename);
	else if( *sourcename != '\0' )
		return mprintf("pool/%s/%c/%s",component,sourcename[0],sourcename);
	else
		return NULL;
}

char *calc_filekey(const char *component,const char *sourcename,const char *filename) {
	if( sourcename[0] == 'l' && sourcename[1] == 'i' && sourcename[2] == 'b' && sourcename[3] != '\0' )

		return mprintf("pool/%s/lib%c/%s/%s",component,sourcename[3],sourcename,filename);
	else if( *sourcename != '\0' )
		return mprintf("pool/%s/%c/%s/%s",component,sourcename[0],sourcename,filename);
	else
		return NULL;
}


char *calc_package_basename(const char *name,const char *version,const char *arch) {
	return mprintf("%s_%s_%s.deb",name,version,arch);
}
