/*  This file is part of "reprepro"
 *  Copyright (C) 2005,2006,2007,2009 Bernhard R. Link
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
#include "atoms.h"
#include "signature.h"
#include "uploaderslist.h"

struct upload_condition {
	/* linked list of all sub-nodes */
	/*@null@*/struct upload_condition *next;

	enum upload_condition_type type;
	const struct upload_condition *next_if_true, *next_if_false;
	bool accept_if_true, accept_if_false;
	union {
		/* uc_SECTION, uc_NAME, uc_SOURCE */
		struct uploadnamecheck {
			/* if one is NULL, do not care, allows
			   begin*middle*end or exact match */
			/*@null@*/ char *beginswith, *includes, *endswith, *exactly;
		} string;
		/* uc_COMPONENT, uc_ARCHITECTURE */
		struct uploadatomlistcheck {
			struct atomlist atomlist;
			/* if false, must be subset of list, if true must be superset */
			bool superset;
		} atoms;
	};
};
struct upload_conditions {
	/* condition currently tested */
	const struct upload_condition *current;
	/* always use last next, then decrement */
	int count;
	const struct upload_condition *conditions[];
};

static retvalue upload_conditions_add(struct upload_conditions **c_p, const struct upload_condition *a) {
	int newcount;
	struct upload_conditions *n;

	if( *c_p == NULL )
		newcount = 1;
	else
		newcount = (*c_p)->count + 1;
	n = realloc(*c_p, sizeof(struct upload_conditions)
			+ newcount * sizeof(const struct upload_condition*));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->current = NULL;
	n->count = newcount;
	n->conditions[newcount - 1] = a;
	*c_p = n;
	return RET_OK;
}

struct uploader {
	struct uploader *next;
	size_t len;
	char *reversed_fingerprint;
	struct upload_condition permissions;
	bool allow_subkeys;
};

struct uploaders {
	struct uploaders *next;
	size_t reference_count;
	char *filename;
	size_t filename_len;

	struct uploader *by_fingerprint;
	struct upload_condition anyvalidkeypermissions;
	struct upload_condition unsignedpermissions;
	struct upload_condition anybodypermissions;
} *uploaderslists = NULL;

static void uploadpermission_release(struct upload_condition *p) {
	struct upload_condition *h, *f = NULL;

	assert( p != NULL );

	do {
		h = p->next;
		switch( p->type ) {
			case uc_SOURCENAME:
				free(p->string.beginswith);
				free(p->string.includes);
				free(p->string.endswith);
				free(p->string.exactly);
				break;

			case uc_ALWAYS:
			case uc_REJECTED:
				assert( p->type != uc_REJECTED );
				break;
		}
		free(f);
		/* next one must be freed: */
		f = h;
		/* and processed: */
		p = h;
	} while( p != NULL );
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
	uploadpermission_release(&u->anyvalidkeypermissions);
	uploadpermission_release(&u->anybodypermissions);
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

static retvalue find_key_and_add(struct uploaders *u, struct upload_conditions **c_p, const struct signature *s) {
	size_t len, i, primary_len;
	char *reversed;
	const char *fingerprint, *primary_fingerprint;
	char *reversed_primary_key;
	const struct uploader *uploader;
	retvalue r;

	assert( u != NULL );

	fingerprint = s->keyid;
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
"Search for appropriate rules in the uploaders file might fail.\n",
					c, c, fingerprint);
			break;
		}
		reversed[i] = c;
	}
	len = i;
	reversed[len] = '\0';

	/* hm, this only sees the key is expired when it is kind of late... */
	primary_fingerprint = s->primary_keyid;
	primary_len = strlen(primary_fingerprint);
	reversed_primary_key = alloca(len+1);
	if( FAILEDTOALLOC(reversed_primary_key) )
		return RET_ERROR_OOM;

	for( i = 0 ; i < primary_len ; i++ ) {
		char c = primary_fingerprint[primary_len-i-1];
		if( c >= 'a' && c <= 'f' )
			c -= 'a' - 'A';
		else if( c == 'x' && primary_len-i-1 == 1 &&
				primary_fingerprint[0] == '0' )
			break;
		if( ( c < '0' || c > '9' ) && ( c <'A' && c > 'F') ) {
			fprintf(stderr,
"Strange character '%c'(=%hhu) in fingerprint/key-id '%s'.\n"
"Search for appropriate rules in the uploaders file might fail.\n",
					c, c, primary_fingerprint);
			break;
		}
		reversed_primary_key[i] = c;
	}
	primary_len = i;
	reversed_primary_key[primary_len] = '\0';

	for( uploader = u->by_fingerprint ; uploader != NULL ; uploader = uploader->next ) {
		/* TODO: allow ignoring */
		if( s->state != sist_valid )
			continue;
		if( uploader->allow_subkeys ) {
			if( uploader->len > primary_len )
				continue;
			if( memcmp(uploader->reversed_fingerprint,
						reversed_primary_key,
						uploader->len) != 0 )
				continue;
		} else {
			if( uploader->len > len )
				continue;
			if( memcmp(uploader->reversed_fingerprint,
						reversed, uploader->len) != 0 )
				continue;
		}
		r = upload_conditions_add(c_p, &uploader->permissions);
		if( RET_WAS_ERROR(r) )
			return r;
		/* no break here, as a key might match
		 * multiple specifications of different length */
	}
	return RET_OK;
}

retvalue uploaders_permissions(struct uploaders *u, const struct signatures *signatures, struct upload_conditions **c_p) {
	struct upload_conditions *conditions = NULL;
	retvalue r;
	int j;

	r = upload_conditions_add(&conditions,
			&u->anybodypermissions);
	if( RET_WAS_ERROR(r) )
		return r;
	if( signatures == NULL ) {
		/* signatures.count might be 0 meaning there is
		 * something lile a gpg header but we could not get
		 * keys, because of a gpg error or because of being
		 * compiling without libgpgme */
		r = upload_conditions_add(&conditions,
				&u->unsignedpermissions);
		if( RET_WAS_ERROR(r) ) {
			free(conditions);
			return r;
		}
	}
	if( signatures != NULL && signatures->validcount > 0 ) {
		r = upload_conditions_add(&conditions,
				&u->anyvalidkeypermissions);
		if( RET_WAS_ERROR(r) ) {
			free(conditions);
			return r;
		}
	}
	if( signatures != NULL ) {
		for( j = 0 ; j < signatures->count ; j++ ) {
			r = find_key_and_add(u, &conditions,
					&signatures->signatures[j]);
			if( RET_WAS_ERROR(r) ) {
				free(conditions);
				return r;
			}
		}
	}
	*c_p = conditions;
	return RET_OK;
}

/* uc_FAILED means rejected, uc_ACCEPTED means can go in */
enum upload_condition_type uploaders_nextcondition(struct upload_conditions *c) {
	/* return the next non-trivial condition: */

	while( true ) {
		while( c->current != NULL ) {
			assert( c->current->type > uc_REJECTED );
			if( c->current->type == uc_ALWAYS ) {
				if( c->current->accept_if_true )
					return uc_ACCEPTED;
				c->current = c->current->next_if_true;
			} else
				return c->current->type;
		}
		if( c->count == 0 )
			return uc_REJECTED;
		c->count--;
		c->current = c->conditions[c->count];
	}
	/* not reached */
}

bool uploaders_verifystring(struct upload_conditions *conditions, const char *name) {
	bool matches = false;
	const struct upload_condition *c = conditions->current;

	assert( c != NULL );
	assert( /* c->type == uc_SECTION || */  c->type == uc_SOURCENAME );

	if( c->string.exactly != NULL ) {
		matches = strcmp(c->string.exactly, name) == 0;
	} else {
		size_t lb, lm, le, l = strlen(name);

		if( c->string.beginswith != NULL )
			lb = strlen(c->string.beginswith);
		else
			lb = 0;
		if( c->string.includes != NULL )
			lm = strlen(c->string.includes);
		else
			lm = 0;
		if( c->string.endswith != NULL )
			le = strlen(c->string.endswith);
		else
			le = 0;

		matches = lb + lm + le <= l;
		if( matches && lb > 0 ) {
			if( memcmp(c->string.beginswith, name, lb) != 0 )
				matches = false;
		}
		if( matches && le > 0 ) {
			if( memcmp(name + (l - le),
						c->string.endswith, le) != 0 )
				matches = false;
		}
		if( matches && lm > 0 ) {
			const char *p = strstr(name + lb, c->string.includes);
			if( p == NULL || (size_t)(p - name) > (l - le - lm) )
				matches = false;
		}
	}
	if( matches?c->accept_if_true:c->accept_if_false )
		return true;
	if( matches )
		conditions->current = c->next_if_true;
	else
		conditions->current = c->next_if_false;
	return false;
}

static struct upload_condition *addfingerprint(struct uploaders *u, const char *fingerprint, size_t len, bool allow_subkeys, const char *filename, long lineno) {
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
		if( uploader->len != len )
			continue;
		if( memcmp(uploader->reversed_fingerprint, reversed, len) != 0 )
			continue;
		if( uploader->allow_subkeys != allow_subkeys )
			continue;
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
	uploader->allow_subkeys = allow_subkeys;
	return &uploader->permissions;
}

static inline const char *overkey(const char *p) {
	while( (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')
			|| (*p >= 'A' && *p <= 'F') ) {
		p++;
	}
	return p;
}

static retvalue parse_condition(const char *filename, long lineno, int column, const char **pp, /*@out@*/struct upload_condition *condition) {
	const char *p = *pp;

	memset( condition, 0, sizeof(struct upload_condition));

	if( *p == '\0' || p[0] != '*' || !xisspace(p[1]) ) {
		fprintf(stderr, "%s:%lu:%u: permission class expected after 'allow' keyword!\n(Currently only '*' is supported.)\n", filename, lineno, column + (int)(p-*pp));
		return RET_ERROR;
	}
	p++;
	*pp = p;

	condition->type = uc_ALWAYS;
	condition->accept_if_true = true;
	/* allocate a new fallback-node: */
	condition->next = calloc(1, sizeof(struct upload_condition));
	if( FAILEDTOALLOC(condition->next) )
		return RET_ERROR_OOM;
	condition->next_if_false = condition->next;
	condition->next->type = uc_ALWAYS;
	assert(!condition->next->accept_if_true);
	return RET_OK;
}

static void condition_add(struct upload_condition *permissions, struct upload_condition *c) {
	if( permissions->next == NULL ) {
		/* first condition, as no fallback yet allocated */
		*permissions = *c;
		memset(c, 0, sizeof(struct upload_condition));
	} else {
		struct upload_condition *last;

		last = permissions->next;
		assert( last != NULL );
		while( last->next != NULL )
			last = last->next;

		/* the very last is always the fallback-node to which all
		 * other conditions fall back if they have no decision */
		assert(last->type = uc_ALWAYS);
		assert(!last->accept_if_true);

		*last = *c;
		memset(c, 0, sizeof(struct upload_condition));
	}
}

static inline retvalue parseuploaderline(char *buffer, const char *filename, size_t lineno, struct uploaders *u) {
	retvalue r;
	const char *p, *q, *qq;
	size_t l;
	struct upload_condition *permissions;
	struct upload_condition condition;

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
	r = parse_condition(filename, lineno, (1+p-buffer), &p, &condition);
	if( RET_WAS_ERROR(r) )
		return r;
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
		bool allow_subkeys = false;

		p += 3;
		while( *p != '\0' && xisspace(*p) )
			p++;
		if( p[0] == '0' && p[1] == 'x' )
			p += 2;
		q = overkey(p);
		if( *p == '\0' || (*q !='\0' && !xisspace(*q) && *q != '+') || q==p ) {
			fprintf(stderr, "%s:%lu:%u: key id or fingerprint expected!\n", filename, (long)lineno, (int)(1+q-buffer));
			return RET_ERROR;
		}
		qq = q;
		while( xisspace(*qq) )
			qq++;
		if( *qq == '+' ) {
			qq++;
			allow_subkeys = true;
		}
		while( xisspace(*qq) )
			qq++;
		if( *qq != '\0' ) {
			fprintf(stderr, "%s:%lu:%u: unexpected data after 'key <fingerprint>' statement!\n\n", filename, (long)lineno, (int)(1+qq-buffer));
			if( *q == ' ' )
				fprintf(stderr, " Hint: no spaces allowed in fingerprint specification.\n");
			return RET_ERROR;
		}
		permissions = addfingerprint(u, p, q-p, allow_subkeys,
				filename, lineno);
		if( permissions == NULL )
			return RET_ERROR_OOM;
		condition_add(permissions, &condition);
	} else if( strncmp(p, "unsigned",8) == 0 && (p[8]=='\0' || xisspace(p[8])) ) {
		p+=8;
		if( *p != '\0' ) {
			fprintf(stderr, "%s:%lu:%u: unexpected data after 'unsigned' statement!\n", filename, (long)lineno, (int)(1+p-buffer));
			return RET_ERROR;
		}
		condition_add(&u->unsignedpermissions, &condition);
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
		condition_add(&u->anyvalidkeypermissions, &condition);
	} else if( strncmp(p, "anybody", 7) == 0 && (p[7] == '\0' || xisspace(p[7])) ) {
		p+=7;
		while( *p != '\0' && xisspace(*p) )
			p++;
		if( *p != '\0' ) {
			fprintf(stderr, "%s:%lu:%u: unexpected data after 'anybody' statement!\n", filename, (long)lineno, (int)(1+p-buffer));
			return RET_ERROR;
		}
		condition_add(&u->anybodypermissions, &condition);
	} else {
		fprintf(stderr, "%s:%lu:%u: 'key', 'unsigned', 'anybody' or 'any key' expected!\n", filename, (long)lineno, (int)(1+p-buffer));
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue uploaders_load(/*@out@*/struct uploaders **list, const char *filename) {
	char *fullfilename = NULL;
	FILE *f;
	size_t lineno=0;
	char buffer[1025];
	struct uploaders *u;
	retvalue r;

	if( filename[0] != '/' ) {
		fullfilename = calc_conffile(filename);
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
	if( FAILEDTOALLOC(u) ) {
		(void)fclose(f);
		free(fullfilename);
		return RET_ERROR_OOM;
	}
	/* reject by default */
	u->unsignedpermissions.type = uc_ALWAYS;
	u->anyvalidkeypermissions.type = uc_ALWAYS;
	u->anybodypermissions.type = uc_ALWAYS;

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

retvalue uploaders_get(/*@out@*/struct uploaders **list, const char *filename) {
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
		r = uploaders_load(&u, filename);
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
