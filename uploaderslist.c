/*  This file is part of "reprepro"
 *  Copyright (C) 2005,2006,2007 Bernhard R. Link
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
#include <unistd.h>
#include <stdlib.h>
#include <alloca.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "uploaderslist.h"

extern int verbose;

struct uploader {
	struct uploader *next;
	size_t len;
	char *reversed_fingerprint;
	struct uploadpermissions permissions;
};

struct uploaders {
	struct uploaders *next;
	size_t reference_count;
	char *filename;
	size_t filename_len;

	struct uploader *by_fingerprint;
	struct uploadpermissions defaultpermissions;
	struct uploadpermissions unsignedpermissions;
} *uploaderslists = NULL;

static void uploadpermission_release(struct uploadpermissions *p) {
	assert( p != NULL );
}

static void uploader_free(struct uploader *u) {
	if( u == NULL )
		return;
	free(u->reversed_fingerprint);
	uploadpermission_release(&u->permissions);
	free(u);
}

static void uploaders_free(struct uploaders *u) {
	if( u == NULL )
		return;
	while( u->by_fingerprint != NULL ) {
		struct uploader *next = u->by_fingerprint->next;

		uploader_free(u->by_fingerprint);
		u->by_fingerprint = next;
	}
	uploadpermission_release(&u->defaultpermissions);
	uploadpermission_release(&u->unsignedpermissions);
	free(u->filename);
	free(u);
}

void uploaders_unlock(struct uploaders *u) {
	if( u->reference_count > 1 ) {
		u->reference_count--;
	} else {
		struct uploaders **p = &uploaderslists;

		assert( u->reference_count == 1);
		/* avoid double free: */
		if( u->reference_count == 0 )
			return;

		while( *p != NULL && *p != u )
			p = &(*p)->next;
		assert( p != NULL && *p == u );
		if( *p == u ) {
			*p = u->next;
			uploaders_free(u);
		}
	}
}

retvalue uploaders_unsignedpermissions(struct uploaders *u, const struct uploadpermissions **permissions) {
	assert( u != NULL );
	*permissions = &u->unsignedpermissions;
	return RET_OK;
}

retvalue uploaders_permissions(struct uploaders *u,const char *fingerprint,const struct uploadpermissions **permissions) {
	size_t len,i;
	char *reversed;
	const struct uploader *uploader;

	assert( u != NULL );

	if( u->by_fingerprint == NULL ) {
		*permissions = &u->defaultpermissions;
		return RET_OK;
	}
	assert( fingerprint != NULL );
	len = strlen(fingerprint);
	reversed = alloca(len+1);
	if( reversed == NULL )
		return RET_ERROR_OOM;
	for( i = 0 ; i < len ; i++ ) {
		char c = fingerprint[len-i-1];
		if( c >= 'a' && c <= 'f' )
			c -= 'a' - 'A';
		else if( c == 'x' && len-i-1 == 1 && fingerprint[0] == '0' )
			break;
		if( ( c < '0' || c > '9' ) && ( c <'A' && c > 'F') ) {
			fprintf(stderr,
"Strange character '%c'(=%hhu) in fingerprint '%s'.\n"
"Finding appropriate rules in the uploaders file might fail due to this.\n",
					c,c,fingerprint);
			break;
		}
		reversed[i] = c;
	}
	len = i;
	reversed[len] = '\0';
	for( uploader = u->by_fingerprint ; uploader != NULL ; uploader = uploader->next ) {
		if( uploader->len > len )
			continue;
		if( memcmp(uploader->reversed_fingerprint, reversed, uploader->len) != 0 )
			continue;
		*permissions = &uploader->permissions;
		return RET_OK;
	}
	*permissions = &u->defaultpermissions;
	return RET_OK;
}


static struct uploadpermissions *addfingerprint(struct uploaders *u, const char *fingerprint, size_t len, const char *filename, long lineno) {
	size_t i;
	char *reversed = malloc(len+1);
	struct uploader *uploader, **last;

	if( reversed == NULL )
		return NULL;
	for( i = 0 ; i < len ; i++ ) {
		char c = fingerprint[len-i-1];
		if( c >= 'a' && c <= 'f' )
			c -= 'a' - 'A';
		assert( ( c >= '0' && c <= '9' ) || ( c >= 'A' || c <= 'F') );
		reversed[i] = c;
	}
	reversed[len] = '\0';
	last = &u->by_fingerprint;
	for( uploader = u->by_fingerprint ; uploader != NULL ; uploader = *(last = &uploader->next) ) {
		if( uploader->len < len ) {
			if( memcmp(uploader->reversed_fingerprint,
			            reversed, uploader->len) == 0 ) {
				fprintf(stderr, "%s:%lu: Warning: key '%.*s' shadowed by earlier shorter definition for '%.*s'!\n", filename, lineno, (int)len, fingerprint, (int)uploader->len, fingerprint);
			}
			continue;
		} else if( uploader->len > len ) {
			if( memcmp(uploader->reversed_fingerprint, reversed, len) == 0 ) {
				fprintf(stderr, "%s:%lu: Warning: key '%.*s' might shadow earlier longer definition in future versions of reprepro!\n", filename, lineno, (int)len, fingerprint);
			}
			continue;
		}
		if( memcmp(uploader->reversed_fingerprint, reversed, len) != 0 )
			continue;
		fprintf(stderr, "%s:%lu: Warning: key '%*s' defined multiple times!\n", filename, lineno, (int)len, fingerprint);
		free(reversed);
		return &uploader->permissions;
	}
	assert( *last == NULL );
	uploader = calloc(1,sizeof(struct uploader));
	if( uploader == NULL )
		return NULL;
	*last = uploader;
	uploader->reversed_fingerprint = reversed;
	uploader->len = len;
	return &uploader->permissions;
}

static inline const char *overkey(const char *p) {
	while( (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')
			|| (*p >= 'A' && *p <= 'F') ) {
		p++;
	}
	return p;
}

static inline retvalue parseuploaderline(char *buffer, const char *filename, size_t lineno, struct uploaders *u) {
	const char *p,*q;
	size_t l;
	struct uploadpermissions *permissions;

	l = strlen(buffer);
	if( l == 0 )
		return RET_NOTHING;
	if( buffer[l-1] != '\n' ) {
		if( l >= 1024 )
			fprintf(stderr, "%s:%lu:1024: Overlong line!\n", filename, (long)lineno);
		else
			fprintf(stderr, "%s:%lu:%lu: Unterminated line!\n", filename, (long)lineno,(long)l);
		return RET_ERROR;
	}
	do {
		buffer[--l] = '\0';
	} while( l > 0 && xisspace(buffer[l-1]) );

	p = buffer;
	while( *p != '\0' && xisspace(*p) )
		p++;
	if( *p == '\0' || *p == '#' )
		return RET_NOTHING;

	if( strncmp(p,"allow",5) != 0 || !xisspace(p[5]) ) {
		fprintf(stderr, "%s:%lu:%u: 'allow' keyword expected! (no other statement has yet been implemented)\n", filename, (long)lineno, (int)(1+p-buffer));
		return RET_ERROR;
	}
	p+=5;
	while( *p != '\0' && xisspace(*p) )
		p++;
	if( *p == '\0' || p[0] != '*' || !xisspace(p[1]) ) {
		fprintf(stderr, "%s:%lu:%u: permission class expected after 'allow' keyword!\n(Currently only '*' is supported.)\n", filename, (long)lineno, (int)(1+p-buffer));
		return RET_ERROR;
	}
	p++;
	while( *p != '\0' && xisspace(*p) )
		p++;
	if( strncmp(p,"by",2) != 0 || !xisspace(p[2]) ) {
		fprintf(stderr, "%s:%lu:%u: 'by' keyword expected!\n", filename, (long)lineno, (int)(1+p-buffer));
		return RET_ERROR;
	}
	p += 2;
	while( *p != '\0' && xisspace(*p) )
		p++;
	if( strncmp(p,"key",3) == 0 && (p[3] == '\0' || xisspace(p[3])) ) {
		p += 3;
		while( *p != '\0' && xisspace(*p) )
			p++;
		if( p[0] == '0' && p[1] == 'x' )
			p += 2;
		q = overkey(p);
		if( *p == '\0' || (*q !='\0' && !xisspace(*q)) || q==p ) {
			fprintf(stderr, "%s:%lu:%u: key id or fingerprint expected!\n", filename, (long)lineno, (int)(1+q-buffer));
			return RET_ERROR;
		}
		if( *q != '\0' ) {
			fprintf(stderr, "%s:%lu:%u: unexpected data after 'key <fingerprint>' statement!\n\n", filename, (long)lineno, (int)(1+q-buffer));
			if( *q == ' ' )
				fprintf(stderr, " Hint: fingerprint has to be specified without spaces.\n");
			return RET_ERROR;
		}
		permissions = addfingerprint(u, p, q-p, filename, lineno);
		if( permissions == NULL )
			return RET_ERROR_OOM;
	} else if( strncmp(p, "unsigned",8) == 0 && (p[8]=='\0' || xisspace(p[8])) ) {
		p+=8;
		if( *p != '\0' ) {
			fprintf(stderr, "%s:%lu:%u: unexpected data after 'unsigned' statement!\n", filename, (long)lineno, (int)(1+p-buffer));
			return RET_ERROR;
		}
		permissions = &u->unsignedpermissions;
	} else if( strncmp(p, "any",3) == 0 && xisspace(p[3]) ) {
		p+=3;
		while( *p != '\0' && xisspace(*p) )
			p++;
		if( strncmp(p, "key", 3) != 0 || (p[3]!='\0' && !xisspace(p[3])) ) {
			fprintf(stderr, "%s:%lu:%u: 'key' keyword expected after 'any' keyword!\n", filename, (long)lineno, (int)(1+p-buffer));
			return RET_ERROR;
		}
		p += 3;
		if( *p != '\0' ) {
			fprintf(stderr, "%s:%lu:%u: unexpected data after 'any key' statement!\n", filename, (long)lineno, (int)(1+p-buffer));
			return RET_ERROR;
		}
		permissions = &u->defaultpermissions;
	} else {
		fprintf(stderr, "%s:%lu:%u: 'key', 'unsigned' or 'any key' expected!\n", filename, (long)lineno, (int)(1+p-buffer));
		return RET_ERROR;
	}
	permissions->allowall = true;
	return RET_OK;
}



static retvalue uploaders_load(/*@out@*/struct uploaders **list, const char *confdir, const char *filename) {
	char *fullfilename = NULL;
	FILE *f;
	size_t lineno=0;
	char buffer[1025];
	struct uploaders *u;
	retvalue r;

	if( filename[0] != '/' ) {
		fullfilename = calc_dirconcat(confdir, filename);
		if( fullfilename == NULL )
			return RET_ERROR_OOM;
		filename = fullfilename;
	}
	f = fopen(filename, "r");
	if( f == NULL ) {
		int e = errno;
		fprintf(stderr, "Error opening '%s': %s\n", filename, strerror(e));
		free(fullfilename);
		return RET_ERRNO(e);
	}
	u = calloc(1,sizeof(struct uploaders));
	if( u == NULL ) {
		(void)fclose(f);
		free(fullfilename);
		return RET_ERROR_OOM;
	}

	while( fgets(buffer,1024,f) != NULL ) {
		lineno++;
		r = parseuploaderline(buffer,filename,lineno,u);
		if( RET_WAS_ERROR(r) ) {
			(void)fclose(f);
			free(fullfilename);
			uploaders_free(u);
			return r;
		}
	}
	if( fclose(f) != 0 ) {
		int e = errno;
		fprintf(stderr, "Error reading '%s': %s\n", filename, strerror(e));
		free(fullfilename);
		uploaders_free(u);
		return RET_ERRNO(e);
	}
	free(fullfilename);
	*list = u;
	return RET_OK;
}

retvalue uploaders_get(/*@out@*/struct uploaders **list, const char *confdir, const char *filename) {
	retvalue r;
	struct uploaders *u;
	size_t len;

	assert( filename != NULL );

	len = strlen(filename);
	u = uploaderslists;
	while( u != NULL && ( u->filename_len != len ||
	                      memcmp(u->filename,filename,len) != 0 ) )
		u = u->next;
	if( u == NULL ) {
		r = uploaders_load(&u, confdir, filename);
		if( !RET_IS_OK(r) )
			return r;
		assert( u != NULL );
		u->filename = strdup(filename);
		if( u->filename == NULL ) {
			uploaders_free(u);
			return RET_ERROR_OOM;
		}
		u->filename_len = len;
		u->next = uploaderslists;
		u->reference_count = 1;
		uploaderslists = u;
	} else
		u->reference_count++;
	*list = u;
	return RET_OK;
}
