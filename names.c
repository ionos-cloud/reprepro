/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007 Bernhard R. Link
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

#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"

extern int verbose;

// This escaping is quite harsh, but so nothing bad can happen...
static inline size_t escapedlen(const char *p) {
	size_t l = 0;
	if( *p == '-' ) {
		l = 3;
		p++;
	}
	while( *p != '\0' ) {
		if( (*p < 'A' || *p > 'Z' ) && (*p < 'a' || *p > 'z' ) && ( *p < '0' || *p > '9') && *p != '-' )
			l +=3;
		else
			l++;
		p++;
	}
	return l;
}

static inline char *escapecpy(char *dest,const char *orig) {
	static char hex[16] = "0123456789ABCDEF";
	if( *orig == '-' ) {
		orig++;
		*dest = '%'; dest++;
		*dest = '2'; dest++;
		*dest = 'D'; dest++;
	}
	while( *orig != '\0' ) {
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

char *calc_downloadedlistfile(const char *listdir,const char *codename,const char *origin,const char *component,const char *architecture,const char *packagetype) {
	size_t l_listdir,len,l_packagetype;
	char *result,*p;

	l_listdir = strlen(listdir),
	len = escapedlen(codename) + escapedlen(origin) + escapedlen(component) + escapedlen(architecture);
	l_packagetype = strlen(packagetype);
	p = result = malloc(l_listdir + len + l_packagetype + 7);
	if( result == NULL )
		return result;
	memcpy(p,listdir,l_listdir);
	p += l_listdir;
	*p = '/'; p++;
	p = escapecpy(p,codename);
	*p = '_'; p++;
	p = escapecpy(p,origin);
	*p = '_'; p++;
	strcpy(p,packagetype); p += l_packagetype;
	*p = '_'; p++;
	p = escapecpy(p,component);
	*p = '_'; p++;
	p = escapecpy(p,architecture);
	*p = '\0';
	return result;
}

char *calc_downloadedlistpattern(const char *codename) {
	size_t len;
	char *result,*p;

	len = escapedlen(codename);
	p = result = malloc(len + 2);
	if( result == NULL )
		return result;
	p = escapecpy(p,codename);
	*p = '_'; p++;
	*p = '\0';
	return result;
}

char *calc_identifier(const char *codename,const char *component,const char *architecture,const char *packagetype) {
	// TODO: add checks to all data possibly given into here...
	assert( strchr(codename,'|') == NULL && strchr(component,'|') == NULL && strchr(architecture,'|') == NULL );
	assert( codename != NULL ); assert( component != NULL );
	assert( architecture != NULL ); assert( packagetype != NULL );
	if( packagetype[0] == 'u' )
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
char *calc_dirconcatn(const char *str1,const char *str2,size_t len2) {
	size_t len1;
	char *r;

	assert(str1 != NULL);
	assert(str2 != NULL);
	len1 = strlen(str1);
	if( (size_t)(len1+len2+2) < len1 || (size_t)(len1+len2+2) < len2 )
		return NULL;
	r = malloc(len1+len2+2);
	if( r == NULL )
		return NULL;
	strcpy(r,str1);
	r[len1] = '/';
	memcpy(r+len1+1,str2,len2);
	r[len1+1+len2] = '\0';
	return r;
}

char *calc_dirconcat3(const char *str1,const char *str2,const char *str3) {
	return mprintf("%s/%s/%s",str1,str2,str3);
}
char *calc_dirsuffixconcat(const char *str1,const char *str2,const char *suffix) {
	return mprintf("%s/%s.%s",str1,str2,suffix);
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


char *calc_binary_basename(const char *name,const char *version,const char *arch,const char *packagetype) {
	const char *v;
	assert( name != NULL && version != NULL && arch != NULL && packagetype != NULL );
	v = strchr(version,':');
	if( v != NULL )
		v++;
	else
		v = version;
	return mprintf("%s_%s_%s.%s",name,v,arch,packagetype);
}

char *calc_source_basename(const char *name,const char *version) {
	const char *v = strchr(version,':');
	if( v != NULL )
		v++;
	else
		v = version;
	return mprintf("%s_%s.dsc",name,v);
}

retvalue calc_extractsize(const char *checksum, off_t *size) {
	off_t value;
#if 0
	/* over possible extensions (not yet implemented otherwise) */
	while( *checksum == ':' ) {
		checksum++;
		while( *checksum != ' ' && *checksum != '\0' )
			checksum++;
		if( *checksum == ' ' )
			checksum++;
	}
#endif
	/* over md5 part */
	while( *checksum != ' ' && *checksum != '\0' )
		checksum++;
	if( *checksum == ' ' )
		checksum++;
	if( *checksum < '0' || *checksum > '9' )
		return RET_NOTHING;
	value = *checksum - '0';
	checksum++;
	while( *checksum >= '0' && *checksum <= '9' ) {
		value *= 10;
		value += (*checksum - '0');
		checksum++;
	}
	if( *checksum != '\0' )
		return RET_NOTHING;
	*size = value;
	return RET_OK;
}

char *calc_concatmd5andsize(const char *md5sum,const char *size) {
	/* this is not the only reference, as there are prints
	 * with size as ofs_t, too */
	return mprintf("%s %s",md5sum,size);
}

char *names_concatmd5sumandsize(const char *md5start,const char *md5end,const char *sizestart,const char *sizeend) {
	char *result;

	result = malloc(2+(md5end-md5start)+(sizeend-sizestart));
	if( result== NULL )
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
			strlist_done(files);
			return r;
		}
	}
	return r;

}

void names_overversion(const char **version, bool epochsuppressed) {
	const char *n = *version;
	bool hadepoch = epochsuppressed;

	if( *n < '0' || *n > '9' ) {
		if( (*n < 'a' || *n > 'z') && (*n < 'A' || *n > 'Z') )
			return;
	} else
		n++;
	while( *n >= '0' && *n <= '9' )
		n++;
	if( *n == ':' ) {
		hadepoch = true;
		n++;
	}
	while( ( *n >= '0' && *n <= '9' ) || ( *n >= 'a' && *n <= 'z')
			|| ( *n >= 'A' && *n <= 'Z' ) || *n == '.' || *n == '~'
			|| *n == '-' || *n == '+' || (hadepoch && *n == ':') )
		n++;
	*version = n;
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
	while( xisspace(*md5) )
		md5++;
	if( *md5 == '\0' )
		return RET_NOTHING;

	/* ... and ends with the following spaces. */
	md5end = md5;
	while( *md5end != '\0' && !xisspace(*md5end) )
		md5end++;
	if( !xisspace(*md5end) ) {
		fprintf(stderr,"Expecting more data after md5sum!\n");
		return RET_ERROR;
	}
	/* Then the size of the file is expected: */
	size = md5end;
	while( xisspace(*size) )
		size++;
	sizeend = size;
	while( xisdigit(*sizeend) )
		sizeend++;
	if( !xisspace(*sizeend) ) {
		fprintf(stderr,"Error in parsing size or missing space afterwards!\n");
		return RET_ERROR;
	}
	/* Then the filename */
	fn = sizeend;
	while( xisspace(*fn) )
		fn++;
	fnend = fn;
	while( *fnend != '\0' && !xisspace(*fnend) )
		fnend++;

	filen = strndup(fn,fnend-fn);
	if( filen == NULL )
		return RET_ERROR_OOM;
	if( md5sum != NULL ) {
		md5as = malloc((md5end-md5)+2+(sizeend-size));
		if( md5as == NULL ) {
			free(filen);
			return RET_ERROR_OOM;
		}
		strncpy(md5as,md5,md5end-md5);
		md5as[md5end-md5] = ' ';
		strncpy(md5as+1+(md5end-md5),size,sizeend-size);
		md5as[(md5end-md5)+1+(sizeend-size)] = '\0';

		*md5sum = md5as;
	}
	if( filename != NULL )
		*filename = filen;
	else
		free(filen);

	return RET_OK;
}

char *calc_trackreferee(const char *codename,const char *sourcename,const char *sourceversion) {
	return	mprintf("%s %s %s",codename,sourcename,sourceversion);
}

char *calc_changes_basename(const char *name,const char *version,const struct strlist *architectures) {
	size_t name_l, version_l, l;
	int i;
	char *n,*p;

	name_l = strlen(name);
	version_l = strlen(version);
	l = name_l + version_l + sizeof("__.changes");

	for( i = 0 ; i < architectures->count ; i++ ) {
		l += strlen(architectures->values[i]);
		if( i != 0 )
			l++;
	}
	n = malloc(l);
	if( n == NULL )
		return n;
	p = n;
	memcpy(p, name, name_l); p+=name_l;
	*(p++) = '_';
	memcpy(p, version, version_l); p+=version_l;
	*(p++) = '_';
	for( i = 0 ; i < architectures->count ; i++ ) {
		size_t a_l = strlen(architectures->values[i]);
		if( i != 0 )
			*(p++) = '+';
		assert( (size_t)((p+a_l)-n) < l );
		memcpy(p, architectures->values[i], a_l);
		p += a_l;
	}
	assert( (size_t)(p-n) < l-8 );
	memcpy(p, ".changes", 9 ); p += 9;
	assert( *(p-1) == '\0' );
	assert( (size_t)(p-n) == l );
	return n;
}
