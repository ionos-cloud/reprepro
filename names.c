/*  This file is part of "reprepro"
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
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"


// This escaping is quite harsh, but so nothing bad can happen...
static inline size_t escapedlen(const char *p) {
	size_t l = 0;
	if( *p == '-' ) {
		l = 3;
		p++;
	}
	while( *p ) {
		if( (*p < 'A' || *p > 'Z' ) && (*p < 'a' || *p > 'z' ) && ( *p < '0' || *p > '9') && *p != '-' )
			l +=3;
		else
			l++;
		p++;
	}
	return l;
}

static inline char * escapecpy(char *dest,const char *orig) {
	static char hex[16] = "0123456789ABCDEF";
	if( *orig == '-' ) {
		orig++;
		*dest = '%'; dest++;
		*dest = '2'; dest++;
		*dest = 'D'; dest++;
	}
	while( *orig ) {
		if( (*orig < 'A' || *orig > 'Z' ) && (*orig < 'a' || *orig > 'z' ) && ( *orig < '0' || *orig > '9') && *orig != '-' ) {
			*dest = '%'; dest++;
			*dest = hex[(*orig >> 4)& 0xF ]; dest++;
			*dest = hex[*orig & 0xF ]; dest++;
		} else {
			*dest = *orig;
			dest++;
		}
		orig++;
	}
	return dest;
}

char *calc_downloadedlistfile(const char *listdir,const char *codename,const char *origin,const char *component,const char *architecture,const char *suffix) {
	size_t l_listdir,len,l_suffix;
	char *result,*p;
	
	l_listdir = strlen(listdir),
	len = escapedlen(codename) + escapedlen(origin) + escapedlen(component) + escapedlen(architecture);
	l_suffix = strlen(suffix);
	p = result = malloc(l_listdir + len + l_suffix + 7);
	if( result == NULL )
		return result;
	memcpy(p,listdir,l_listdir);
	p += l_listdir; 
	*p = '/'; p++;
	p = escapecpy(p,codename);
	*p = '_'; p++;
	p = escapecpy(p,origin);
	*p = '_'; p++;
	p = escapecpy(p,component);
	*p = '_'; p++;
	p = escapecpy(p,architecture);
	*p = '_'; p++;
	strcpy(p,suffix); p += l_suffix;
	*p = '\0';
	return result;
}

char *calc_identifier(const char *codename,const char *component,const char *architecture,const char *suffix) {
	// TODO: add checks to all data possibly given into here...
	assert( index(codename,'|') == NULL && index(component,'|') == NULL && index(architecture,'|') == NULL );
	assert( codename && component && architecture && suffix );
	if( suffix[0] == 'u' ) 
		return mprintf("u|%s|%s|%s",codename,component,architecture);
	else
		return mprintf("%s|%s|%s",codename,component,architecture);
}

char *calc_addsuffix(const char *str1,const char *str2) {
	return mprintf("%s.%s",str1,str2);
}

char *calc_dirconcat(const char *str1,const char *str2) {
	return mprintf("%s/%s",str1,str2);
}

char *calc_dirconcat3(const char *str1,const char *str2,const char *str3) {
	return mprintf("%s/%s/%s",str1,str2,str3);
}

char *calc_comprconcat(const char *str1,const char *str2,const char *str3,indexcompression compr) {
	assert(compr >= 0 && compr <= ic_max );

	switch( compr ) {
		case ic_uncompressed:
			return mprintf("%s/%s/%s",str1,str2,str3);
		case ic_gzip:
			return mprintf("%s/%s/%s.gz",str1,str2,str3);
		default:
			assert(0);
			return NULL;
	}
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


char *calc_binary_basename(const char *name,const char *version,const char *arch,const char *suffix) {
	const char *v;
	assert( name && version && arch && suffix );
	v = index(version,':');
	if( v )
		v++;
	else
		v = version;
	return mprintf("%s_%s_%s.%s",name,v,arch,suffix);
}

char *calc_source_basename(const char *name,const char *version) {
	const char *v = index(version,':');
	if( v )
		v++;
	else
		v = version;
	return mprintf("%s_%s.dsc",name,v);
}

char *calc_concatmd5andsize(const char *md5sum,const char *size) {
	/* this is not the only reference, as there are prints
	 * with size as ofs_t, too */
	return mprintf("%s %s",md5sum,size);
}

char *names_concatmd5sumandsize(const char *md5start,const char *md5end,const char *sizestart,const char *sizeend) {
	char *result;

	result = malloc(2+(md5end-md5start)+(sizeend-sizestart));
	if( !result)
		return result;
	memcpy(result,md5start,(md5end-md5start));
	result[(md5end-md5start)] = ' ';
	memcpy(result+(md5end-md5start)+1,sizestart,sizeend-sizestart);
	result[(md5end-md5start)+1+(sizeend-sizestart)] = '\0';
	
	return result;

}

/* Create a strlist consisting out of calc_dirconcat'ed entries of the old */
retvalue calc_dirconcats(const char *directory, const struct strlist *basefilenames,
						struct strlist *files) {
	retvalue r;
	int i;

	assert(directory != NULL && basefilenames != NULL && files != NULL );

	r = strlist_init_n(basefilenames->count,files);
	if( RET_WAS_ERROR(r) )
		return r;

	r = RET_NOTHING;
	for( i = 0 ; i < basefilenames->count ; i++ ) {
		char *file;

		file = calc_dirconcat(directory,basefilenames->values[i]);
		if( file == NULL ) {
			strlist_done(files);
			return RET_ERROR_OOM;
		}
		r = strlist_add(files,file);
		if( RET_WAS_ERROR(r) ) {
			free(file);
			strlist_done(files);
			return r;
		}
	}
	return r;

}

static inline int ispkgnamechar(char c) {
// Policy says, only lower case letters are allowed,
// though dak allows upper case, too. I hope nothing
// needs them.

	return  ( c == '+' ) || ( c == '-') || ( c == '.' )
		|| (( c >= 'a') && ( c <= 'z' ))
		|| (( c >= '0') && ( c <= '9' ));
}

void names_overpkgname(const char **name_end) {
	const char *n = *name_end;

	if( ( *n < '0' || *n > '9' ) && ( *n < 'a' || *n > 'z' ) ) {
		return;
	}
	n++;

	while( ispkgnamechar(*n) )
		n++;

	*name_end = n;
}

retvalue names_checkpkgname(const char *name) {
	const char *n = name;

	if( ( *n < '0' || *n > '9' ) && ( *n < 'a' || *n > 'z' ) ) {
		fprintf(stderr,"Not starting with a lowercase alphanumeric character: '%s'!\n",name);
		return RET_ERROR;
	}
	n++;
	if( !ispkgnamechar(*n) ) {
		fprintf(stderr,"Not at least 2 characters long: '%s'!\n",name);
		return RET_ERROR;
	}
	n++;

	while( ispkgnamechar(*n) )
		n++;

	if( *n != '\0' ) {
		fprintf(stderr,"Unexpected Character '%c' in '%s'!\n",*n,name);
		return RET_ERROR;
	}
	return RET_OK;
}

void names_overversion(const char **version) {
	const char *n = *version;

	if( *n < '0' || *n > '9' )
		return;
	n++;
	while( *n >= '0' && *n <= '9' )
		n++;
	if( *n == ':' )
		n++;
	while( ( *n >= '0' && *n <= '9' ) || ( *n >= 'a' && *n <= 'z')
			|| ( *n >= 'A' && *n <= 'Z' ) || *n == '.'
			|| *n == '-' || *n == '+' )
		n++;
	*version = n;
}

retvalue names_checkversion(const char *version) {
	const char *n = version;
// jennifer(dak) uses "^([0-9]+:)?[0-9A-Za-z\.\-\+:]+$", thus explicitly allowing
// an epoch and having colons in the rest. As those are nasty anyway, we should
// perhaps forbid them.
// an epoch, when the version number may use colons seems not very efficient.
// though also see names_checkbasename for problems with double colons...

	if( *n == '\0' ) {
		fprintf(stderr,"An empty string is no valid version number!\n");
		return RET_ERROR;
	}
	while( ( *n >= '0' && *n <= '9' ) || ( *n >= 'a' && *n <= 'z')
			|| ( *n >= 'A' && *n <= 'Z' ) || *n == '.'
			|| *n == '-' || *n == '+' || *n == ':' )
		n++;

	if( *n != '\0' ) {
		fprintf(stderr,"Unexpected Character '%c' in '%s'!\n",*n,version);
		return RET_ERROR;
	}
	return RET_OK;

}

retvalue names_checkbasename(const char *basename) {
	const char *n = basename;
// DAK allows '~', though I do not know, where it could come from...
// Note that while versions may have colons after the epoch, jennifier
// rejects files having colons in them, so we do the same...

	if( *n == '\0' ) {
		fprintf(stderr,"An empty string is no valid filename!\n");
		return RET_ERROR;
	}
	while( ( *n >= '0' && *n <= '9' ) || ( *n >= 'a' && *n <= 'z')
			|| ( *n >= 'A' && *n <= 'Z' ) || *n == '.'
			|| *n == '-' || *n == '+' )
		n++;

	if( *n != '\0' ) {
		fprintf(stderr,"Unexpected Character '%c' in '%s'!\n",*n,basename);
		return RET_ERROR;
	}
	return RET_OK;
}

/* split a "<md5> <size> <filename>" into md5sum and filename */
retvalue calc_parsefileline(const char *fileline,char **filename,char **md5sum) {
	const char *md5,*md5end,*size,*sizeend,*fn,*fnend;
	char *md5as,*filen;

	assert( fileline != NULL );
	if( *fileline == '\0' )
		return RET_NOTHING;

	/* the md5sums begins after the (perhaps) heading spaces ...  */
	md5 = fileline;
	while( isspace(*md5) )
		md5++;
	if( *md5 == '\0' )
		return RET_NOTHING;

	/* ... and ends with the following spaces. */
	md5end = md5;
	while( *md5end != '\0' && !isspace(*md5end) )
		md5end++;
	if( !isspace(*md5end) ) {
		fprintf(stderr,"Expecting more data after md5sum!\n");
		return RET_ERROR;
	}
	/* Then the size of the file is expected: */
	size = md5end;
	while( isspace(*size) )
		size++;
	sizeend = size;
	while( isdigit(*sizeend) )
		sizeend++;
	if( !isspace(*sizeend) ) {
		fprintf(stderr,"Error in parsing size or missing space afterwards!\n");
		return RET_ERROR;
	}
	/* Then the filename */
	fn = sizeend;
	while( isspace(*fn) )
		fn++;
	fnend = fn;
	while( *fnend != '\0' && !isspace(*fnend) )
		fnend++;

	filen = strndup(fn,fnend-fn);
	if( !filen )
		return RET_ERROR_OOM;
	if( md5sum ) {
		md5as = malloc((md5end-md5)+2+(sizeend-size));
		if( !md5as ) {
			free(filen);
			return RET_ERROR_OOM;
		}
		strncpy(md5as,md5,md5end-md5);
		md5as[md5end-md5] = ' ';
		strncpy(md5as+1+(md5end-md5),size,sizeend-size);
		md5as[(md5end-md5)+1+(sizeend-size)] = '\0';
	
		*md5sum = md5as;
	}
	if( filename )
		*filename = filen;
	else
		free(filen);

	return RET_OK;
}
