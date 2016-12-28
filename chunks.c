/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2007 Bernhard R. Link
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include "error.h"
#include "chunks.h"
#include "names.h"

/* point to a specified field in a chunk */
static const char *chunk_getfield(const char *name, const char *chunk) {
	size_t l;

	if (chunk == NULL)
		return NULL;
	l = strlen(name);
	while (*chunk != '\0') {
		if (strncasecmp(name, chunk, l) == 0 && chunk[l] == ':') {
			chunk += l+1;
			return chunk;
		}
		while (*chunk != '\n' && *chunk != '\0')
			chunk++;
		if (*chunk == '\0')
			return NULL;
		chunk++;
	}
	return NULL;
}

/* get the content of the given field, including all following lines, in a format
 * that may be put into chunk_replacefields
static retvalue chunk_getcontent(const char *chunk, const char *name, char **value) {
	const char *field;
	char *val;
	const char *b, *e;

	assert(value != NULL);
	field = chunk_getfield(name, chunk);
	if (field == NULL)
		return RET_NOTHING;

	b = field;
	* jump over spaces at the beginning *
	if (xisspace(*b))
		b++;

	* search for the end *
	e = b;
	do {
		while (*e != '\n' && *e != '\0')
			e++;
		if (*e != '\0')
			e++;
	} while (*e != ' ' && *e != '\t' && *e != '\0');

	if (e > b && *e == '\0')
		e--;
	* remove trailing newline *
	if (e > b && *e == '\n')
		e--;
	if (e > b)
		val = strndup(b, e - b + 1);
	else
		val = strdup("");
	if (FAILEDTOALLOC(val))
		return RET_ERROR_OOM;
	*value = val;
	return RET_OK;
}
*/

/* look for name in chunk. returns RET_NOTHING if not found */
retvalue chunk_getvalue(const char *chunk, const char *name, char **value) {
	const char *field;
	char *val;
	const char *b, *e;

	assert(value != NULL);
	field = chunk_getfield(name, chunk);
	if (field == NULL)
		return RET_NOTHING;

	b = field;
	/* jump over spaces at the beginning */
	while (*b != '\0' && (*b == ' ' || *b == '\t'))
		b++;
	/* search for the end */
	e = b;
	while (*e != '\n' && *e != '\0')
		e++;
	/* remove trailing spaces */
	while (e > b && xisspace(*e))
		e--;
	if (!xisspace(*e))
		val = strndup(b, e - b + 1);
	else
		val = strdup("");
	if (FAILEDTOALLOC(val))
		return RET_ERROR_OOM;
	*value = val;
	return RET_OK;
}

retvalue chunk_getextralinelist(const char *chunk, const char *name, struct strlist *strlist) {
	retvalue r;
	const char *f, *b, *e;
	char *v;

	f = chunk_getfield(name, chunk);
	if (f == NULL)
		return RET_NOTHING;
	strlist_init(strlist);
	/* walk over the first line */
	while (*f != '\0' && *f != '\n')
		f++;
	/* nothing there is an empty list */
	if (*f == '\0')
		return RET_OK;
	f++;
	/* while lines begin with ' ' or '\t', add them */
	while (*f == ' ' || *f == '\t') {
		while (*f != '\0' && xisblank(*f))
			f++;
		b = f;
		while (*f != '\0' && *f != '\n')
			f++;
		e = f;
		while (e > b && *e != '\0' && xisspace(*e))
			e--;
		if (!xisspace(*e))
			v = strndup(b, e - b + 1);
		else
			v = strdup("");
		if (FAILEDTOALLOC(v)) {
			strlist_done(strlist);
			return RET_ERROR_OOM;
		}
		r = strlist_add(strlist, v);
		if (!RET_IS_OK(r)) {
			strlist_done(strlist);
			return r;
		}
		if (*f == '\0')
			return RET_OK;
		f++;
	}
	return RET_OK;
}

retvalue chunk_getwholedata(const char *chunk, const char *name, char **value) {
	const char *f, *p, *e;
	bool afternewline = false;
	char *v;

	f = chunk_getfield(name, chunk);
	if (f == NULL)
		return RET_NOTHING;
	while (*f == ' ')
		f++;
	for (e = p = f ; *p != '\0' ; p++) {
		if (afternewline) {
			if (*p == ' ' || *p == '\t')
				afternewline = false;
			else if (*p != '\r')
				break;
		} else {
			if (*p == '\n') {
				e = p;
				afternewline = true;
			}
		}
	}
	if (!afternewline && *p == '\0')
		e = p;
	v = strndup(f, e - f);
	if (FAILEDTOALLOC(v))
		return RET_ERROR_OOM;
	*value = v;
	return RET_OK;
}

retvalue chunk_getwordlist(const char *chunk, const char *name, struct strlist *strlist) {
	retvalue r;
	const char *f, *b;
	char *v;

	f = chunk_getfield(name, chunk);
	if (f == NULL)
		return RET_NOTHING;
	strlist_init(strlist);
	while (*f != '\0') {
		/* walk over spaces */
		while (*f != '\0' && xisspace(*f)) {
			if (*f == '\n') {
				f++;
				if (*f != ' ' && *f != '\t')
					return RET_OK;
			} else
				f++;
		}
		if (*f == '\0')
			return RET_OK;
		b = f;
		/* search for end of word */
		while (*f != '\0' && !xisspace(*f))
			f++;
		v = strndup(b, f - b);
		if (FAILEDTOALLOC(v)) {
			strlist_done(strlist);
			return RET_ERROR_OOM;
		}
		r = strlist_add(strlist, v);
		if (!RET_IS_OK(r)) {
			strlist_done(strlist);
			return r;
		}
	}
	return RET_OK;
}

retvalue chunk_getuniqwordlist(const char *chunk, const char *name, struct strlist *strlist) {
	retvalue r;
	const char *f, *b;
	char *v;

	f = chunk_getfield(name, chunk);
	if (f == NULL)
		return RET_NOTHING;
	strlist_init(strlist);
	while (*f != '\0') {
		/* walk over spaces */
		while (*f != '\0' && xisspace(*f)) {
			if (*f == '\n') {
				f++;
				if (*f != ' ' && *f != '\t')
					return RET_OK;
			} else
				f++;
		}
		if (*f == '\0')
			return RET_OK;
		b = f;
		/* search for end of word */
		while (*f != '\0' && !xisspace(*f))
			f++;
		v = strndup(b, f - b);
		if (FAILEDTOALLOC(v)) {
			strlist_done(strlist);
			return RET_ERROR_OOM;
		}
		r = strlist_adduniq(strlist, v);
		if (!RET_IS_OK(r)) {
			strlist_done(strlist);
			return r;
		}
	}
	return RET_OK;
}

retvalue chunk_gettruth(const char *chunk, const char *name) {
	const char *field;

	field = chunk_getfield(name, chunk);
	if (field == NULL)
		return RET_NOTHING;
	while (*field == ' ' || *field == '\t')
		field++;
	if ((field[0] == 'f' || field[0] == 'F') &&
			(field[1] == 'a' || field[1] == 'A') &&
			(field[2] == 'l' || field[2] == 'L') &&
			(field[3] == 's' || field[3] == 'S') &&
			(field[4] == 'e' || field[4] == 'E')) {
		return RET_NOTHING;
	}
	if ((field[0] == 'n' || field[0] == 'N') &&
			(field[1] == 'o' || field[1] == 'O')) {
		return RET_NOTHING;
	}
	// TODO: strict check?
	return RET_OK;
}
/* return RET_OK, if field is found, RET_NOTHING, if not */
retvalue chunk_checkfield(const char *chunk, const char *name){
	const char *field;

	field = chunk_getfield(name, chunk);
	if (field == NULL)
		return RET_NOTHING;

	return RET_OK;
}

/* Parse a package/source-field: ' *value( ?\(version\))? *' */
retvalue chunk_getname(const char *chunk, const char *name, char **pkgname, bool allowversion) {
	const char *field, *name_end, *p;

	field = chunk_getfield(name, chunk);
	if (field == NULL)
		return RET_NOTHING;
	while (*field != '\0' && *field != '\n' && xisspace(*field))
		field++;
	name_end = field;
	/* this has now checked somewhere else for correctness and
	 * is only a pure separation process:
	 * (as package(version) is possible, '(' must be checked) */
	while (*name_end != '\0' && *name_end != '\n' && *name_end != '('
			&& !xisspace(*name_end))
		name_end++;
	p = name_end;
	while (*p != '\0' && *p != '\n' && xisspace(*p))
		p++;
	if (name_end == field ||
		(*p != '\0' && *p != '\n' &&
		  (!allowversion || *p != '('))) {
		if (*field == '\n' || *field == '\0') {
			fprintf(stderr, "Error: Field '%s' is empty!\n", name);
		} else {
			fprintf(stderr,
"Error: Field '%s' contains unexpected character '%c'!\n",
					name, *p);
		}
		return RET_ERROR;
	}
	if (*p == '(') {
		while (*p != '\0' && *p != '\n' && *p != ')')
			// TODO: perhaps check for wellformed version
			p++;
		if (*p != ')') {
			fprintf(stderr,
"Error: Field '%s' misses closing parenthesis!\n", name);
			return RET_ERROR;
		}
		p++;
	}
	while (*p != '\0' && *p != '\n' && xisspace(*p))
		p++;
	if (*p != '\0' && *p != '\n') {
		fprintf(stderr,
"Error: Field '%s' contains trailing junk starting with '%c'!\n", name, *p);
		return RET_ERROR;
	}

	*pkgname = strndup(field, name_end - field);
	if (FAILEDTOALLOC(*pkgname))
		return RET_ERROR_OOM;
	return RET_OK;

}

/* Parse a package/source-field: ' *value( ?\(version\))? *' */
retvalue chunk_getnameandversion(const char *chunk, const char *name, char **pkgname, char **version) {
	const char *field, *name_end, *p;
	char *v;

	field = chunk_getfield(name, chunk);
	if (field == NULL)
		return RET_NOTHING;
	while (*field != '\0' && *field != '\n' && xisspace(*field))
		field++;
	name_end = field;
	/* this has now checked somewhere else for correctness and
	 * is only a pure separation process:
	 * (as package(version) is possible, '(' must be checked) */
	while (*name_end != '\0' && *name_end != '\n' && *name_end != '('
			&& !xisspace(*name_end))
		name_end++;
	p = name_end;
	while (*p != '\0' && *p != '\n' && xisspace(*p))
		p++;
	if (name_end == field || (*p != '\0' && *p != '\n' && *p != '(')) {
		if (*field == '\n' || *field == '\0') {
			fprintf(stderr, "Error: Field '%s' is empty!\n", name);
		} else {
			fprintf(stderr,
"Error: Field '%s' contains unexpected character '%c'!\n", name, *p);
		}
		return RET_ERROR;
	}
	if (*p == '(') {
		const char *version_begin;

		p++;
		while (*p != '\0' && *p != '\n' && xisspace(*p))
			p++;
		version_begin = p;
		while (*p != '\0' && *p != '\n' && *p != ')'  && !xisspace(*p))
			// TODO: perhaps check for wellformed version
			p++;
		v = strndup(version_begin, p - version_begin);
		if (FAILEDTOALLOC(v))
			return RET_ERROR_OOM;
		while (*p != '\0' && *p != '\n' && *p != ')'  && xisspace(*p))
			p++;
		if (*p != ')') {
			free(v);
			if (*p == '\0' || *p == '\n')
				fprintf(stderr,
"Error: Field '%s' misses closing parenthesis!\n",
						name);
			else
				fprintf(stderr,
"Error: Field '%s' has multiple words after '('!\n",
						name);
			return RET_ERROR;
		}
		p++;
	} else {
		v = NULL;
	}
	while (*p != '\0' && *p != '\n' && xisspace(*p))
		p++;
	if (*p != '\0' && *p != '\n') {
		free(v);
		fprintf(stderr,
"Error: Field '%s' contains trailing junk starting with '%c'!\n",
				name, *p);
		return RET_ERROR;
	}

	*pkgname = strndup(field, name_end - field);
	if (FAILEDTOALLOC(*pkgname)) {
		free(v);
		return RET_ERROR_OOM;
	}
	*version = v;
	return RET_OK;

}

/* Add this the <fields to add> to <chunk> before <beforethis> field,
 * replacing older fields of this name, if they are already there. */

char *chunk_replacefields(const char *chunk, const struct fieldtoadd *toadd, const char *beforethis, bool maybemissing) {
	const char *c, *ce;
	char *newchunk, *n;
	size_t size, len_beforethis;
	const struct fieldtoadd *f;
	retvalue result;
	bool fieldsadded = false;

	assert (chunk != NULL && beforethis != NULL);

	if (toadd == NULL)
		return NULL;

	c = chunk;

	/* calculate the maximal size we might end up with */
	size = 2 + strlen(c);
	f = toadd;
	while (f != NULL) {
		if (f->data != NULL)
			size += 3 + f->len_field + f->len_data;
		f = f->next;
	}

	newchunk = n = malloc(size);
	if (FAILEDTOALLOC(n))
		return NULL;

	len_beforethis = strlen(beforethis);

	result = RET_NOTHING;
	do {
		/* are we at the place to add the fields yet? */
		if (!fieldsadded && strncasecmp(c, beforethis, len_beforethis) == 0
				&& c[len_beforethis] == ':') {
			/* add them now: */
			f = toadd;
			while (f != NULL) {
				if (f->data != NULL) {
					memcpy(n, f->field, f->len_field);
					n += f->len_field;
					*n = ':'; n++;
					*n = ' '; n++;
					memcpy(n, f->data, f->len_data);
					n += f->len_data;
					*n = '\n'; n++;
				}
				f = f->next;
			}
			result = RET_OK;
			fieldsadded = true;
		}
		/* is this one of the fields we added/will add? */
		f = toadd;
		while (f != NULL) {
			if (strncasecmp(c, f->field, f->len_field) == 0
					&& c[f->len_field] == ':')
				break;
			f = f->next;
		}
		/* search the end of the field */
		ce = c;
		do {
			while (*ce != '\n' && *ce != '\0')
				ce++;
			if (*ce == '\0')
				break;
			ce++;
		} while (*ce == ' ' || *ce == '\t');

		/* copy it, if it is not to be ignored */

		if (f == NULL && ce-c > 0) {
			memcpy(n, c, ce -c);
			n += ce-c;
		}

		/* and proceed with the next */
		c = ce;

	} while (*c != '\0' && *c != '\n');

	if (n > newchunk && *(n-1) != '\n')
		*(n++) = '\n';
	if (maybemissing && !fieldsadded) {
		/* add them now, if they are allowed to come later */
		f = toadd;
		while (f != NULL) {
			if (f->data != NULL) {
				memcpy(n, f->field, f->len_field);
				n += f->len_field;
				*n = ':'; n++;
				*n = ' '; n++;
				memcpy(n, f->data, f->len_data);
				n += f->len_data;
				*n = '\n'; n++;
			}
			f = f->next;
		}
		result = RET_OK;
		fieldsadded = true;
	}
	*n = '\0';

	assert (n-newchunk < 0 || (size_t)(n-newchunk) <= size-1);

	if (result == RET_NOTHING) {
		fprintf(stderr,
"Could not find field '%s' in chunk '%s'!!!\n",
				beforethis, chunk);
		assert(false);
	}

	return newchunk;
}

struct fieldtoadd *aodfield_new(const char *field, const char *data, struct fieldtoadd *next) {
	struct fieldtoadd *n;

	assert(field != NULL);

	n = NEW(struct fieldtoadd);
	if (FAILEDTOALLOC(n)) {
		addfield_free(next);
		return NULL;
	}
	n->field = field;
	n->len_field = strlen(field);
	n->data = data;
	if (data != NULL)
		n->len_data = strlen(data);
	else
		n->len_data = 0;
	n->next = next;
	return n;
}
struct fieldtoadd *addfield_new(const char *field, const char *data, struct fieldtoadd *next) {
	struct fieldtoadd *n;

	assert(field != NULL && data != NULL);

	n = NEW(struct fieldtoadd);
	if (FAILEDTOALLOC(n)) {
		addfield_free(next);
		return NULL;
 	}
	n->field = field;
	n->len_field = strlen(field);
	n->data = data;
	n->len_data = strlen(data);
	n->next = next;
	return n;
}
struct fieldtoadd *deletefield_new(const char *field, struct fieldtoadd *next) {
	struct fieldtoadd *n;

	assert(field != NULL);

	n = NEW(struct fieldtoadd);
	if (FAILEDTOALLOC(n)) {
		addfield_free(next);
		return NULL;
	}
	n->field = field;
	n->len_field = strlen(field);
	n->data = NULL;
	n->len_data = 0;
	n->next = next;
	return n;
}
struct fieldtoadd *addfield_newn(const char *field, const char *data, size_t len, struct fieldtoadd *next) {
	struct fieldtoadd *n;

	n = NEW(struct fieldtoadd);
	if (FAILEDTOALLOC(n)) {
		addfield_free(next);
		return NULL;
	}
	n->field = field;
	n->len_field = strlen(field);
	n->data = data;
	n->len_data = len;
	n->next = next;
	return n;
}
void addfield_free(struct fieldtoadd *f) {
	struct fieldtoadd *g;

	while (f != NULL) {
		g = f->next;
		free(f);
		f = g;
	}
}

char *chunk_replacefield(const char *chunk, const char *fieldname, const char *data, bool maybemissing) {
	struct fieldtoadd toadd;

	toadd.field = fieldname;
	toadd.len_field = strlen(fieldname);
	toadd.data = data;
	toadd.len_data = strlen(data);
	toadd.next = NULL;
	return chunk_replacefields(chunk, &toadd, fieldname, maybemissing);
}

/* Add field <firstfieldname> as first field with value data, and remove
 * all other fields of that name (and of name alsoremove if that is != NULL), */

char *chunk_normalize(const char *chunk, const char *firstfieldname, const char *data) {
	const char *c, *ce;
	char *newchunk, *n;
	size_t size;
	size_t data_len, field_len;

	assert (chunk != NULL && firstfieldname != NULL && data != NULL);
	data_len = strlen(data);
	field_len = strlen(firstfieldname);
	c = chunk;

	/* calculate the maximal size we might end up with */
	size = 2 + strlen(c) + 3 + data_len + field_len;

	newchunk = n = malloc(size);
	if (FAILEDTOALLOC(n))
		return NULL;

	memcpy(n, firstfieldname, field_len); n += field_len;
	*(n++) = ':';
	*(n++) = ' ';
	memcpy(n, data, data_len); n += data_len;
	*(n++) = '\n';
	do {
		bool toremove;

		if (strncasecmp(c, firstfieldname, field_len) == 0
				&& c[field_len] == ':')
			toremove = true;
		else
			toremove = false;
		/* search the end of the field */
		ce = c;
		do {
			while (*ce != '\n' && *ce != '\0')
				ce++;
			if (*ce == '\0')
				break;
			ce++;
		} while (*ce == ' ' || *ce == '\t');

		/* copy it, if it is not to be ignored */

		if (!toremove && ce-c > 0) {
			memcpy(n, c, ce-c);
			n += ce-c;
		}
		/* and proceed with the next */
		c = ce;
	} while (*c != '\0' && *c != '\n');
	if (n > newchunk && *(n-1) != '\n')
		*(n++) = '\n';
	*n = '\0';
	return newchunk;
}

const char *chunk_getstart(const char *start, size_t len, bool commentsallowed) {
	const char *s, *l;

	s = start; l = start + len;
	while (s < l && (*s == ' ' || *s == '\t' ||
			*s == '\r' || *s =='\n'))
		s++;
	/* ignore leading comments (even full paragraphs of them) */
	while (commentsallowed && s < l && *s == '#') {
		while (s < l && *s != '\n')
			s++;
		while (s < l && (*s == ' ' || *s == '\t' ||
				*s == '\r' ||
				*s =='\n'))
			s++;
	}
	return s;
}

const char *chunk_over(const char *e) {
	while (*e != '\0') {
		if (*(e++) == '\n') {
			while (*e =='\r')
				e++;
			if (*e == '\n')
				return e+1;
		}
	}
	return e;
}

/* this is a bit wastefull, as with normally perfect formatted input, it just
 * writes everything to itself in a inefficent way. But when there are \r
 * in it or spaces before it or stuff like that, it will be in perfect
 * form afterwards. */
/* Write the first chunk found in the first len bytes after start
 * to buffer and set next to the next data found after it.
 * buffer can be a different buffer may be the buffer start is in
 * (as long as start is bigger than buffer).
 * buffer must be big enough to store up to len+1 bytes */
size_t chunk_extract(char *buffer, const char *start, size_t len, bool commentsallowed, const char **next) {
	const char *e, *n, *l;
	char *p;

	p = buffer;
	l = start + len;
	e = chunk_getstart(start, len, commentsallowed);
	n = NULL;
	while (e < l && *e != '\0') {
		if (*e == '\r') {
			e++;
		} else if (*e == '\n') {
			*(p++) = *(e++);
			n = e;
			while (n < l && *n =='\r')
				n++;
			if (n < l && *n == '\n')
				break;
			e = n;
			n = NULL;
		} else {
			*(p++) = *(e++);
		}
	}

	if (n == NULL) {
		n = e;
		assert (n == l || *n == '\0');
		assert ((p - buffer) <= (n - start));
		*p = '\0';
	} else {
		assert (n < l && *n == '\n');
		n++;
		assert (p - buffer < n - start);
		*p = '\0';
		while (n < l && (*n == '\n' || *n =='\r'))
			n++;
	}
	*next = n;
	return p - buffer;
}

