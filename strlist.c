/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2005,2007 Bernhard R. Link
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "strlist.h"

bool strlist_in(const struct strlist *strlist, const char *element) {
	int c;
	char **t;

	assert(strlist != NULL);

	c = strlist->count;
	t = strlist->values;
	while (c-- != 0) {
		if (strcmp(*(t++), element) == 0)
			return true;
	}
	return false;
}
int strlist_ofs(const struct strlist *strlist, const char *element) {
	int c;
	char **t;

	assert(strlist != NULL);

	c = strlist->count;
	t = strlist->values;
	while (c-- != 0) {
		if (strcmp(*(t++), element) == 0)
			return (t-strlist->values)-1;
	}
	return -1;
}

bool strlist_subset(const struct strlist *strlist, const struct strlist *subset, const char **missing) {
	int c;
	char **t;

	assert(subset != NULL);

	c = subset->count;
	t = subset->values;
	while (c-- != 0) {
		if (!strlist_in(strlist, *(t++))) {
			if (missing != NULL)
				*missing = *(t-1);
			return false;
		}
	}
	return true;

}

retvalue strlist_init_n(int startsize, struct strlist *strlist) {
	assert(strlist != NULL && startsize >= 0);

	if (startsize == 0)
		startsize = 1;
	strlist->count = 0;
	strlist->size = startsize;
	if (startsize > 0) {
		strlist->values = malloc(startsize*sizeof(char *));
		if (FAILEDTOALLOC(strlist->values))
			return RET_ERROR_OOM;
	} else {
		strlist->values = NULL;
	}
	return RET_OK;
}

retvalue strlist_init_singleton(char *value, struct strlist *strlist) {
	assert(strlist != NULL);

	strlist->count = 1;
	strlist->size = 1;
	strlist->values = NEW(char *);
	if (FAILEDTOALLOC(strlist->values)) {
		free(value);
		return RET_ERROR_OOM;
	}
	strlist->values[0] = value;

	return RET_OK;
}

void strlist_init(struct strlist *strlist) {
	assert(strlist != NULL);

	strlist->count = 0;
	strlist->size = 0;
	strlist->values = NULL;
}

void strlist_done(struct strlist *strlist) {
	int c;
	char **t;

	assert(strlist != NULL);

	c = strlist->count;
	t = strlist->values;
	while (c-- != 0) {
		free(*t);
		t++;
	}
	free(strlist->values);
	strlist->values = NULL;
}

retvalue strlist_add(struct strlist *strlist, char *element) {
	char **v;

	assert(strlist != NULL && element != NULL);

	if (strlist->count >= strlist->size) {
		strlist->size += 8;
		v = realloc(strlist->values, strlist->size*sizeof(char *));
		if (FAILEDTOALLOC(v)) {
			free(element);
			return RET_ERROR_OOM;
		}
		strlist->values = v;
	}

	strlist->values[strlist->count++] = element;
	return RET_OK;
}

retvalue strlist_add_dup(struct strlist *strlist, const char *todup) {
	char *element = strdup(todup);

	if (FAILEDTOALLOC(element))
		return RET_ERROR_OOM;
	return strlist_add(strlist, element);
}

retvalue strlist_include(struct strlist *strlist, char *element) {
	char **v;

	assert(strlist != NULL && element != NULL);

	if (strlist->count >= strlist->size) {
		strlist->size += 1;
		v = realloc(strlist->values, strlist->size*sizeof(char *));
		if (FAILEDTOALLOC(v)) {
			free(element);
			return RET_ERROR_OOM;
		}
		strlist->values = v;
	}
	arrayinsert(char *, strlist->values, 0, strlist->count);
	strlist->count++;
	strlist->values[0] = element;
	return RET_OK;
}

retvalue strlist_fprint(FILE *file, const struct strlist *strlist) {
	int c;
	char **p;
	retvalue result;

	assert(strlist != NULL);
	assert(file != NULL);

	c = strlist->count;
	p = strlist->values;
	result = RET_OK;
	while (c > 0) {
		if (fputs(*(p++), file) == EOF)
			result = RET_ERROR;
		if (--c > 0 && fputc(' ', file) == EOF)
			result = RET_ERROR;
	}
	return result;
}

/* replace the contents of dest with those from orig, which get emptied */
void strlist_move(struct strlist *dest, struct strlist *orig) {

	assert(dest != NULL && orig != NULL);

	if (dest == orig)
		return;

	dest->size = orig->size;
	dest->count = orig->count;
	dest->values = orig->values;
	orig->size = orig->count = 0;
	orig->values = NULL;
}

retvalue strlist_adduniq(struct strlist *strlist, char *element) {
	// TODO: is there something better feasible?
	if (strlist_in(strlist, element)) {
		free(element);
		return RET_OK;
	} else
		return strlist_add(strlist, element);

}

bool strlist_intersects(const struct strlist *a, const struct strlist *b) {
	int i;

	for (i = 0 ; i < a->count ; i++)
		if (strlist_in(b, a->values[i]))
			return true;
	return false;
}

char *strlist_concat(const struct strlist *list, const char *prefix, const char *infix, const char *suffix) {
	size_t l, prefix_len, infix_len, suffix_len, line_len;
	char *c, *n;
	int i;

	prefix_len = strlen(prefix);
	infix_len = strlen(infix);
	suffix_len = strlen(suffix);

	l = prefix_len + suffix_len;
	for (i = 0 ; i < list->count ; i++)
		l += strlen(list->values[i]);
	if (list->count > 0)
		l += (list->count-1)*infix_len;
	c = malloc(l + 1);
	if (FAILEDTOALLOC(c))
		return c;
	memcpy(c, prefix, prefix_len);
	n = c + prefix_len;
	for (i = 0 ; i < list->count ; i++) {
		line_len = strlen(list->values[i]);
		memcpy(n, list->values[i], line_len);
		n += line_len;
		if (i+1 < list->count) {
			memcpy(n, infix, infix_len);
			n += infix_len;
		} else {
			memcpy(n, suffix, suffix_len);
			n += suffix_len;
		}
	}
	assert ((size_t)(n-c) == l);
	*n = '\0';
	return c;
}

void strlist_remove(struct strlist *strlist, const char *element) {
	int i, j;

	assert(strlist != NULL);
	assert(element != NULL);

	j = 0;
	for (i = 0 ; i < strlist->count ; i++) {
		if (strcmp(strlist->values[i], element) != 0) {
			if (i != j)
				strlist->values[j] = strlist->values[i];
			j++;
		} else
			free(strlist->values[i]);
	}
	strlist->count = j;
}
