/*  This file is part of "reprepro"
 *  Copyright (C) 2006 Bernhard R. Link
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

#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include "error.h"
#include "chunkedit.h"
#include "names.h"

void cef_free(struct chunkeditfield *f) {
	while (f != NULL) {
		int i;
		struct chunkeditfield *p = f;
		f = f->next;

		for (i = 0 ; i < p->linecount ; i++) {
			free(p->lines[i].words);
			free(p->lines[i].wordlen);
		}
		free(p);
	}
}

struct chunkeditfield *cef_newfield(const char *field, enum cefaction action, enum cefwhen when, unsigned int linecount, struct chunkeditfield *next) {
	struct chunkeditfield *n;

	n = calloc(1, sizeof(struct chunkeditfield) +
			linecount * sizeof(struct cef_line));
	if (FAILEDTOALLOC(n)) {
		cef_free(next);
		return NULL;
	}
	assert(field != NULL);
	n->field = field;
	n->len_field = strlen(field);
	n->action = action;
	n->when = when;
	n->linecount = linecount;
	n->next = next;
	return n;
}


void cef_setdatalen(struct chunkeditfield *cef, const char *data, size_t len) {
	assert (data != NULL || len == 0);

	assert (cef->len_all_data >= cef->len_data);
	cef->len_all_data -= cef->len_data;
	cef->len_all_data += len;
	cef->data = data;
	cef->len_data = len;
	cef->words = NULL;
}

void cef_setdata(struct chunkeditfield *cef, const char *data) {
	cef_setdatalen(cef, data, strlen(data));
}

void cef_setwordlist(struct chunkeditfield *cef, const struct strlist *words) {
	int i; size_t len = 0;

	for (i = 0 ; i < words->count ; i++) {
		len += 1+strlen(words->values[i]);
	}
	if (len > 0)
		len--;
	assert (cef->len_all_data >= cef->len_data);
	cef->len_all_data -= cef->len_data;
	cef->len_all_data += len;
	cef->data = NULL;
	cef->len_data = len;
	cef->words = words;
}

retvalue cef_setline(struct chunkeditfield *cef, int line, int wordcount, ...) {
	va_list ap; int i;
	struct cef_line *l;
	const char *word;
	size_t len;

	assert (line < cef->linecount);
	assert (wordcount > 0);

	l = &cef->lines[line];
	assert (l->wordcount == 0 && l->words == NULL && l->wordlen == NULL);

	l->wordcount = wordcount;
	l->words = nzNEW(wordcount, const char*);
	if (FAILEDTOALLOC(l->words))
		return RET_ERROR_OOM;
	l->wordlen = nzNEW(wordcount, size_t);
	if (FAILEDTOALLOC(l->wordlen)) {
		free(l->words);l->words = NULL;
		return RET_ERROR_OOM;
	}
	va_start(ap, wordcount);
	len = 1; /* newline */
	for (i = 0 ; i < wordcount; i++) {
		word = va_arg(ap, const char*);
		assert(word != NULL);

		l->words[i] = word;
		l->wordlen[i] = strlen(word);
		len += 1 + l->wordlen[i];
	}
	word = va_arg(ap, const char*);
	assert (word == NULL);

	va_end(ap);
	cef->len_all_data += len;
	return RET_OK;
}

retvalue cef_setline2(struct chunkeditfield *cef, int line, const char *hash, size_t hashlen, const char *size, size_t sizelen, int wordcount, ...) {
	va_list ap; int i;
	struct cef_line *l;
	const char *word;
	size_t len;

	assert (line < cef->linecount);
	assert (wordcount >= 0);

	l = &cef->lines[line];
	assert (l->wordcount == 0 && l->words == NULL && l->wordlen == NULL);

	l->wordcount = wordcount + 2;
	l->words = nzNEW(wordcount + 2, const char *);
	if (FAILEDTOALLOC(l->words))
		return RET_ERROR_OOM;
	l->wordlen = nzNEW(wordcount + 2, size_t);
	if (FAILEDTOALLOC(l->wordlen)) {
		free(l->words); l->words = NULL;
		return RET_ERROR_OOM;
	}
	va_start(ap, wordcount);
	len = 1; /* newline */
	l->words[0] = hash;
	l->wordlen[0] = hashlen;
	len += 1 + hashlen;
	l->words[1] = size;
	l->wordlen[1] = sizelen;
	len += 1 + sizelen;
	for (i = 0 ; i < wordcount; i++) {
		word = va_arg(ap, const char*);
		assert(word != NULL);

		l->words[i + 2] = word;
		l->wordlen[i + 2] = strlen(word);
		len += 1 + l->wordlen[i + 2];
	}
	word = va_arg(ap, const char*);
	assert (word == NULL);

	va_end(ap);
	cef->len_all_data += len;
	return RET_OK;
}

static inline int findcef(const struct chunkeditfield *cef, const char *p, size_t len) {
	int result = 0;
	while (cef != NULL) {
		if (cef->len_field == len &&
				strncasecmp(p, cef->field, len) == 0) {
			return result;
		}
		cef = cef->next;
		result++;
	}
	return -1;
}

retvalue chunk_edit(const char *chunk, char **result, size_t *rlen, const struct chunkeditfield *cefs) {
	size_t maxlen;
	int i, processed, count = 0;
	const struct chunkeditfield *cef;
	struct field {
		const struct chunkeditfield *cef;
		size_t startofs, endofs;
		/* next in original chunk */
		int next;
	} *fields;
	const char *p, *q, *e;
	char *n; size_t len;

	maxlen = 1; /* a newline might get missed */
	for (cef = cefs ; cef != NULL ; cef=cef->next) {
		maxlen += cef->len_field + cef->len_all_data + 3; /* ': \n' */
		count ++;
	}
	fields = nzNEW(count, struct field);
	if (FAILEDTOALLOC(fields))
		return RET_ERROR_OOM;
	i = 0;
	for (cef = cefs ; cef != NULL ; cef=cef->next) {
		assert (i < count);
		fields[i++].cef = cef;
	}
	assert (i == count);

	/* get rid of empty or strange lines at the beginning: */
	while (*chunk == ' ' || *chunk == '\t') {
		while (*chunk != '\0' && *chunk != '\n')
			chunk++;
		if (*chunk == '\n')
			chunk++;
	}
	p = chunk;

	while (true) {
		q = p;
		while (*q != '\0' && *q != '\n' && *q != ':')
			q++;
		if (*q == '\0')
			break;
		if (*q == '\n') {
			/* header without colon? what kind of junk is this? */
			q++;
			while (*q == ' ' || *q == '\t') {
				while (*q != '\0' && *q != '\n')
					q++;
				if (*q == '\n')
					q++;

			}
			if (p == chunk)
				chunk = q;
			p = q;
			continue;
		}
		i = findcef(cefs, p, q-p);
		/* find begin and end of data */
		q++;
		while (*q == ' ')
			q++;
		e = q;
		while (*e != '\0' && *e != '\n')
			e++;
		while (e[0] == '\n' && (e[1] == ' ' || e[1] == '\t')) {
			e++;
			while (*e != '\0' && *e != '\n')
				e++;
		}
		if (i < 0) {
			/* not known, we'll have to copy it */
			maxlen += 1+e-p;
			if (*e == '\0')
				break;
			p = e+1;
			continue;
		}
		if (fields[i].endofs == 0) {
			fields[i].startofs = p-chunk;
			fields[i].endofs = e-chunk;
			if (fields[i].cef->action == CEF_KEEP ||
			    fields[i].cef->action == CEF_ADDMISSED)
				maxlen += 1+e-q;
		}
		if (*e == '\0')
			break;
		p = e+1;
	}
	n = malloc(maxlen + 1);
	if (FAILEDTOALLOC(n)) {
		free(fields);
		return RET_ERROR_OOM;
	}
	len = 0;
	for (processed = 0;
	     processed < count && fields[processed].cef->when == CEF_EARLY;
	     processed++) {
		struct field *f = &fields[processed];
		const struct chunkeditfield *ef = f->cef;
		if (ef->action == CEF_DELETE)
			continue;
		if (ef->action == CEF_REPLACE && f->endofs == 0)
			continue;
		if (f->endofs != 0 &&
		    (ef->action == CEF_KEEP ||
		     ef->action == CEF_ADDMISSED)) {
			size_t l = f->endofs - f->startofs;
			assert (maxlen >= len + l);
			memcpy(n+len, chunk + f->startofs, l);
			len +=l;
			n[len++] = '\n';
			continue;
		}
		if (ef->action == CEF_KEEP)
			continue;
		assert (maxlen >= len+ 3+ ef->len_field);
		memcpy(n+len, ef->field, ef->len_field);
		len += ef->len_field;
		n[len++] = ':';
		n[len++] = ' ';
		if (ef->data != NULL) {
			assert (maxlen >= len+1+ef->len_data);
			memcpy(n+len, ef->data, ef->len_data);
			len += ef->len_data;
		} else if (ef->words != NULL) {
			int j;
			for (j = 0 ; j < ef->words->count ; j++) {
				const char *v = ef->words->values[j];
				size_t l = strlen(v);
				if (j > 0)
					n[len++] = ' ';
				memcpy(n+len, v, l);
				len += l;
			}
		}
		for (i = 0 ; i < ef->linecount ; i++) {
			int j;
			n[len++] = '\n';
			for (j = 0 ; j < ef->lines[i].wordcount ; j++) {
				n[len++] = ' ';
				memcpy(n+len, ef->lines[i].words[j],
				               ef->lines[i].wordlen[j]);
				len += ef->lines[i].wordlen[j];
			}
		}
		assert(maxlen > len);
		n[len++] = '\n';
	}
	p = chunk;
	/* now add all headers in between */
	while (true) {
		q = p;
		while (*q != '\0' && *q != '\n' && *q != ':')
			q++;
		if (*q == '\0')
			break;
		if (*q == '\n') {
			/* header without colon? what kind of junk is this? */
			q++;
			while (*q == ' ' || *q == '\t') {
				while (*q != '\0' && *q != '\n')
					q++;
				if (*q == '\n')
					q++;

			}
			p = q;
			continue;
		}
		i = findcef(cefs, p, q-p);
		/* find begin and end of data */
		q++;
		while (*q == ' ')
			q++;
		e = q;
		while (*e != '\0' && *e != '\n')
			e++;
		while (e[0] == '\n' && (e[1] == ' ' || e[1] == '\t')) {
			e++;
			while (*e != '\0' && *e != '\n')
				e++;
		}
		if (i < 0) {
			/* not known, copy it */
			size_t l = e - p;
			assert (maxlen >= len + l);
			memcpy(n+len, p, l);
			len += l;
			n[len++] = '\n';
			if (*e == '\0')
				break;
			p = e+1;
			continue;
		}
		if (*e == '\0')
			break;
		p = e+1;
	}
	for (; processed < count ; processed++) {
		struct field *f = &fields[processed];
		const struct chunkeditfield *ef = f->cef;
		if (ef->action == CEF_DELETE)
			continue;
		if (ef->action == CEF_REPLACE && f->endofs == 0)
			continue;
		if (f->endofs != 0 &&
		    (ef->action == CEF_KEEP ||
		     ef->action == CEF_ADDMISSED)) {
			size_t l = f->endofs - f->startofs;
			assert (maxlen >= len + l);
			memcpy(n+len, chunk + f->startofs, l);
			len +=l;
			n[len++] = '\n';
			continue;
		}
		if (ef->action == CEF_KEEP)
			continue;
		assert (maxlen >= len+ 3+ ef->len_field);
		memcpy(n+len, ef->field, ef->len_field);
		len += ef->len_field;
		n[len++] = ':';
		n[len++] = ' ';
		if (ef->data != NULL) {
			assert (maxlen >= len+1+ef->len_data);
			memcpy(n+len, ef->data, ef->len_data);
			len += ef->len_data;
		} else if (ef->words != NULL) {
			int j;
			for (j = 0 ; j < ef->words->count ; j++) {
				const char *v = ef->words->values[j];
				size_t l = strlen(v);
				if (j > 0)
					n[len++] = ' ';
				memcpy(n+len, v, l);
				len += l;
			}
		}
		for (i = 0 ; i < ef->linecount ; i++) {
			int j;
			n[len++] = '\n';
			for (j = 0 ; j < ef->lines[i].wordcount ; j++) {
				n[len++] = ' ';
				memcpy(n+len, ef->lines[i].words[j],
				               ef->lines[i].wordlen[j]);
				len += ef->lines[i].wordlen[j];
			}
		}
		assert(maxlen > len);
		n[len++] = '\n';
	}
	assert(maxlen >= len);
	n[len] = '\0';
	free(fields);
	*result = realloc(n, len+1);
	if (*result == NULL)
		*result = n;
	*rlen = len;
	return RET_OK;
}
