/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"

extern int verbose;

/* check if the character starting where <character> points
 * at is a overlong one */
static inline bool_t overlongUTF8(const char *character) {
	/* This checks for overlong utf-8 characters.
	 * (as they might mask '.' '\0' or '/' chars).
	 * we assume no filesystem/ar/gpg code will parse
	 * invalid utf8, as we would only be able to rule
	 * this out if we knew it is utf8 we arec coping
	 * with. (Well, you should not use --ignore=validchars
	 * anyway). */
	unsigned char c = *character;

	if( (c & 0xC2 /*11000010*/) == 0xC0 /*11000000*/ ) {
		unsigned char nextc = *(character+1);

		if( (nextc & 0xC0 /*11000000*/ ) != 0x80 /*10000000*/ )
			return FALSE;

		if( (c & 0x3E /* 00111110 */ ) == 0 )
			return TRUE;
		if( c == 0xE0 /*11100000*/ && (nextc & 0x20 /*00100000*/ ) == 0) 
			return TRUE;
		if( c == 0xF0 /*11110000*/ && (nextc & 0x30 /*00110000*/ ) == 0) 
			return TRUE;
		if( c == 0xF8 /*11111000*/ && (nextc & 0x38 /*00111000*/ ) == 0) 
			return TRUE;
		if( c == 0xFC /*11111100*/ && (nextc & 0x3C /*00111100*/ ) == 0) 
			return TRUE;
	}
	return FALSE;
}

/* check if this is something that can be used as directory safely */
retvalue propersourcename(const char *string) {
	const char *s;
	bool_t firstcharacter = TRUE;

	if( string[0] == '\0' ) {
		/* This is not really ignoreable, as this will lead
		 * to paths not normalized, so all checks go wrong */
		fprintf(stderr,"Source name is not allowed to be emtpy!\n");
		return RET_ERROR;
	}
	if( string[0] == '.' ) {
		/* A dot is not only hard to see, it would cause the directory
		 * to become /./.bla, which is quite dangerous. */
		fprintf(stderr,"Source names are not allowed to start with a dot!\n");
		return RET_ERROR;
	}
	s = string;
	while( *s ) {
		if( (*s > 'z' || *s < 'a' ) && 
		    (*s > '9' || *s < '0' ) && 
		    (firstcharacter || 
		     ( *s != '+' && *s != '-' && *s != '.'))) {
			if( *s < ' ' || *s == '/' ) {
				fprintf(stderr,"Character 0x%2x not allowed within sourcename '%s'!\n",(unsigned int)*s,string);
				return RET_ERROR;
			}
			if( overlongUTF8(s) ) {
				fprintf(stderr,"This could contain an overlong UTF8-sequence, rejecting sourcename '%s'!\n",string);
				return RET_ERROR;
			}
			if( !IGNORING(
"Not rejecting","To ignore this",forbiddenchar,"Character 0x%2x not allowed in sourcename: '%s'!\n",(unsigned int)*s,string) ) {
				return RET_ERROR;
			}
			if( *s & 0x80 ) {
				if( !IGNORING(
"Not rejecting","To ignore this",8bit,"8bit character in sourcename: '%s'!\n",string) ) {
					return RET_ERROR;
				}
			}
		}
		s++;
		firstcharacter = FALSE;
	}
	return RET_OK;
}

/* check if this is something that can be used as directory safely */
retvalue properfilename(const char *string) {
	const char *s;

	if( string[0] == '\0' ) {
		fprintf(stderr,"Error: empty filename!\n");
		return RET_ERROR;
	}
	if( (string[0] == '.' && string[1] == '\0') ||
		(string[0] == '.' && string[1] == '.' && string[2] == '\0') ) {
		fprintf(stderr,"Filename not allowed: '%s'!\n",string);
		return RET_ERROR;
	}
	s = string;
	while( *s ) {
		if( *s < ' ' || *s == '/' ) {
			fprintf(stderr,"Character 0x%2x not allowed within filename '%s'!\n",(unsigned int)*s,string);
			return RET_ERROR;
		}
		if( *s & 0x80 ) {
			if( overlongUTF8(s) ) {
				fprintf(stderr,"This could contain an overlong UTF8-sequence, rejecting filename '%s'!\n",string);
				return RET_ERROR;
			}
			if( !IGNORING(
"Not rejecting","To ignore this",8bit,"8bit character in filename: '%s'!\n",string)) {
				return RET_ERROR;
			}
		}
		s++;
	}
	return RET_OK;
}

retvalue properidentiferpart(const char *string) {
	const char *s;

	if( string[0] == '\0' && !IGNORING(
"Ignoring","To ignore this",emptyfilenamepart,"A string to be used of an filename is empty!\n") ) {
		return RET_ERROR;
	}
	s = string;
	while( *s ) {
		if( *s < ' ' || *s == '|' ) {
			fprintf(stderr,"Character 0x%2x not allowed within identifier '%s'!\n",(unsigned int)*s,string);
			return RET_ERROR;
		}
		s++;
	}
	return RET_OK;
}

retvalue properfilenamepart(const char *string) {
	const char *s;

	s = string;
	while( *s ) {
		if( *s < ' ' || *s == '/' ) {
			fprintf(stderr,"Character 0x%2x not allowed within filenamepart '%s'!\n",(unsigned int)*s,string);
			return RET_ERROR;
		}
		if( *s & 0x80 ) {
			if( overlongUTF8(s) ) {
				fprintf(stderr,"This could contain an overlong UTF8-sequence, rejecting filenamepart '%s'!\n",string);
				return RET_ERROR;
			}
			if( !IGNORING(
"Not rejecting","To ignore this",8bit,"8bit character in filenamepart: '%s'!\n",string)) {
				return RET_ERROR;
			}
		}
		s++;
	}
	return RET_OK;
}

retvalue properversion(const char *string) {
	const char *s = string;
	bool_t hadepoch = FALSE;
	bool_t first = TRUE;
	bool_t yetonlydigits = TRUE;

	if( string[0] == '\0' && !IGNORING(
"Ignoring","To ignore this",emptyfilenamepart,"A version string is empty!\n") ) {
		return RET_ERROR;
	}
	if( ( *s < '0' || *s > '9' ) &&
	    (( *s >= 'a' && *s <= 'z') || (*s >='A' && *s <= 'Z'))) {
		/* As there are official packages violating the rule 
		 * of policy 5.6.11 to start with a digit, disabling 
		 * this test, and only omitting a warning. */
		if( verbose >= 0 ) 
			fprintf(stderr,"Warning: Package version '%s' does not start with a digit, violating 'should'-directive in policy 5.6.11\n",string);
	}
	for( ; *s != '\0' ; s++,first=FALSE ) {
		if( (*s <= '9' || *s >= '0' ) ) {
			continue;
		} 
		if( !first && yetonlydigits && *s == ':' ) {
			hadepoch = TRUE;
			continue;
		}
		yetonlydigits = FALSE;
		if( (*s >= 'A' && *s <= 'Z' ) ||
		           (*s >= 'a' || *s <= 'z' )) {
			yetonlydigits = FALSE;
			continue;
		}
		if( first || (*s != '+'  && *s != '-' && 
	            	      *s != '.'  && *s != '~' &&
			      (!hadepoch || *s != ':' ))) {
			if( *s < ' ' || *s == '/' ) {
				fprintf(stderr,"Character 0x%2x not allowed within sourcename '%s'!\n",(unsigned int)*s,string);
				return RET_ERROR;
			}
			if( overlongUTF8(s) ) {
				fprintf(stderr,"This could contain an overlong UTF8-sequence, rejecting version '%s'!\n",string);
				return RET_ERROR;
			}
			if( !IGNORING(
"Not rejecting","To ignore this",forbiddenchar,"Character '%c' not allowed in version: '%s'!\n",*s,string) ) {
				return RET_ERROR;
			}
			if( *s & 0x80 ) {
				if( !IGNORING(
"Not rejecting","To ignore this",8bit,"8bit character in version: '%s'!\n",string) ) {
					return RET_ERROR;
				}
			}
		} 
	}
	return RET_OK;
}

retvalue properfilenames(const struct strlist *names) {
	int i;

	for( i = 0 ; i < names->count ; i ++ ) {
		retvalue r = properfilename(names->values[i]);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

retvalue properpackagename(const char *string) {
	const char *s;
	bool_t firstcharacter = TRUE;

	/* To be able to avoid multiple warnings,
	 * this should always be a subset of propersourcename */

	if( string[0] == '\0' ) {
		/* This is not really ignoreable, as this is a primary
		 * key for our database */
		fprintf(stderr,"Package name is not allowed to be emtpy!\n");
		return RET_ERROR;
	}
	s = string;
	while( *s ) {
		/* DAK also allowed upper case letters last I looked, policy
		 * does not, so they are not allowed without --ignore=forbiddenchar */
		// perhaps some extra ignore-rule for upper case?
		if( (*s > 'z' || *s < 'a' ) && 
		    (*s > '9' || *s < '0' ) && 
		    ( firstcharacter || 
  	    	      (*s != '+' && *s != '-' && *s != '.'))) {
			if( *s < ' ' || *s == '/' ) {
				fprintf(stderr,"Character 0x%2x not allowed within package name '%s'!\n",(unsigned int)*s,string);
				return RET_ERROR;
			}
			if( overlongUTF8(s) ) {
				fprintf(stderr,"This could contain an overlong UTF8-sequence, rejecting package name '%s'!\n",string);
				return RET_ERROR;
			}
			if( !IGNORING(
"Not rejecting","To ignore this",forbiddenchar,"Character 0x%2x not allowed in package name: '%s'!\n",(unsigned int)*s,string) ) {
				return RET_ERROR;
			}
			if( *s & 0x80 ) {
				if( !IGNORING(
"Not rejecting","To ignore this",8bit,"8bit character in package name: '%s'!\n",string) ) {
					return RET_ERROR;
				}
			}
		}
		s++;
		firstcharacter = FALSE;
	}
	return RET_OK;
}

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

static inline char *escapecpy(char *dest,const char *orig) {
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

char *calc_identifier(const char *codename,const char *component,const char *architecture,const char *packagetype) {
	// TODO: add checks to all data possibly given into here...
	assert( index(codename,'|') == NULL && index(component,'|') == NULL && index(architecture,'|') == NULL );
	assert( codename && component && architecture && packagetype );
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


char *calc_binary_basename(const char *name,const char *version,const char *arch,const char *packagetype) {
	const char *v;
	assert( name && version && arch && packagetype );
	v = index(version,':');
	if( v )
		v++;
	else
		v = version;
	return mprintf("%s_%s_%s.%s",name,v,arch,packagetype);
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

void names_overversion(const char **version,bool_t epochsuppressed) {
	const char *n = *version;
	bool_t hadepoch = epochsuppressed;

	if( *n < '0' || *n > '9' ) {
		if( (*n < 'a' || *n > 'z') && (*n < 'A' || *n > 'Z') )
			return;
		else {
			/* As there are packages violating the rule of policy 5.6.11 to
			 * start with a digit, disabling this test, and only omitting a
			 * warning. */
			if( verbose >= 0 ) 
				fprintf(stderr,"Warning: Package version '%s' does not start with a digit, violating 'should'-directive in policy 5.6.11\n",n);
//			return;
		}
	} else
		n++;
	while( *n >= '0' && *n <= '9' )
		n++;
	if( *n == ':' ) {
		hadepoch = TRUE;
		n++;
//TODO: more corectly another check should be here to also look for a digit...
	}
	while( ( *n >= '0' && *n <= '9' ) || ( *n >= 'a' && *n <= 'z')
			|| ( *n >= 'A' && *n <= 'Z' ) || *n == '.'
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
