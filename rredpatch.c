/*  This file is part of "reprepro"
 *  Copyright (C) 2009 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include "error.h"
#include "rredpatch.h"

struct modification {
	/* next item in the list (sorted by oldlinestart) */
	struct modification *next, *previous;
	/* each modification removes an (possible empty) range from
	 * the file and replaces it with an (possible empty) range
	 * of new lines */
	int oldlinestart, oldlinecount, newlinecount;
	size_t len;
	const char *content;
	/* a entry might be followed by one other with the same
	 * oldlinestart (due to merging or inefficient patches),
	 * but always: next->oldlinestart >= oldlinestart + oldlinecount
	 */
};

struct rred_patch {
	int fd;
	/* content of the file mapped with mmap */
	char *data;
	off_t len;
	struct modification *modifications;
	bool alreadyinuse;
};

void modification_freelist(struct modification *p) {
	while (p != NULL) {
		struct modification *m = p;
		p = m->next;
		free(m);
	}
}

struct modification *modification_dup(const struct modification *p) {
	struct modification *first = NULL, *last = NULL;

	for (; p != NULL ; p = p->next) {
		struct modification *m = NEW(struct modification);

		if (FAILEDTOALLOC(m)) {
			modification_freelist(first);
			return NULL;
		}
		*m = *p;
		m->next = NULL;
		m->previous = last;
		if (last == NULL)
			first = m;
		else
			m->previous->next = m;
		last = m;
	}
	return first;
}

struct modification *patch_getmodifications(struct rred_patch *p) {
	struct modification *m;

	assert (!p->alreadyinuse);
	m = p->modifications;
	p->modifications = NULL;
	p->alreadyinuse = true;
	return m;
}

const struct modification *patch_getconstmodifications(struct rred_patch *p) {
	assert (!p->alreadyinuse);
	return p->modifications;
}

static struct modification *modification_freehead(/*@only@*/struct modification *p) {
	struct modification *m = p->next;
	free(p);
	return m;
}

void patch_free(/*@only@*/struct rred_patch *p) {
	if (p->data != NULL)
		(void)munmap(p->data, p->len);
	if (p->fd >= 0)
		(void)close(p->fd);
	modification_freelist(p->modifications);
	free(p);
}

retvalue patch_load(const char *filename, off_t length, struct rred_patch **patch_p) {
	int fd;

	fd = open(filename, O_NOCTTY|O_RDONLY);
	if (fd < 0) {
		int err = errno;
		fprintf(stderr,
"Error %d opening '%s' for reading: %s\n", err, filename, strerror(err));
		return RET_ERRNO(err);
	}
	return patch_loadfd(filename, fd, length, patch_p);

}

retvalue patch_loadfd(const char *filename, int fd, off_t length, struct rred_patch **patch_p) {
	int i;
	struct rred_patch *patch;
	const char *p, *e, *d, *l;
	int number, number2, line;
	char type;
	struct modification *n;
	struct stat statbuf;

	patch = zNEW(struct rred_patch);
	if (FAILEDTOALLOC(patch)) {
		(void)close(fd);
		return RET_ERROR_OOM;
	}
	patch->fd = fd;
	i = fstat(patch->fd, &statbuf);
	if (i != 0) {
		int err = errno;
		fprintf(stderr,
"Error %d retrieving length of '%s': %s\n", err, filename, strerror(err));
		patch_free(patch);
		return RET_ERRNO(err);
	}
	if (length == -1)
		length = statbuf.st_size;
	if (statbuf.st_size != length) {
		int err = errno;
		fprintf(stderr,
"Unexpected size of '%s': expected %lld, got %lld\n", filename,
			(long long)length, (long long)statbuf.st_size);
		patch_free(patch);
		return RET_ERRNO(err);
	}
	if (length == 0) {
		/* handle empty patches gracefully */
		close(patch->fd);
		patch->fd = -1;
		patch->data = NULL;
		patch->len = 0;
		patch->modifications = NULL;
		*patch_p = patch;
		return RET_OK;
	}
	patch->len = length;
	patch->data = mmap(NULL, patch->len, PROT_READ, MAP_PRIVATE,
			patch->fd, 0);
	if (patch->data == MAP_FAILED) {
		int err = errno;
		fprintf(stderr,
"Error %d mapping '%s' into memory: %s\n", err, filename, strerror(err));
		patch_free(patch);
		return RET_ERRNO(err);
	}
	p = patch->data;
	e = p + patch->len;
	line = 1;
	while (p < e) {
		/* <number>,<number>(c|d)\n or <number>(a|i|c|d) */
		d = p;
		number = 0; number2 = -1;
		while (d < e && *d >= '0' && *d <= '9') {
			number = (*d - '0') + 10 * number;
			d++;
		}
		if (d > p && d < e && *d == ',') {
			d++;
			number2 = 0;
			while (d < e && *d >= '0' && *d <= '9') {
				number2 = (*d - '0') + 10 * number2;
				d++;
			}
			if (number2 < number) {
				fprintf(stderr,
"Error parsing '%s': malformed range (2nd number smaller than 1s) at line %d\n",
					filename, line);
				patch_free(patch);
				return RET_ERROR;
			}
		}
		if (d >= e || (*d != 'c' && *d != 'i' && *d != 'a' && *d != 'd')) {
			fprintf(stderr,
"Error parsing '%s': expected rule (c,i,a or d) at line %d\n",
				filename, line);
			patch_free(patch);
			return RET_ERROR;
		}
		type = *d;
		d++;
		while (d < e && *d == '\r')
			d++;
		if (d >= e || *d != '\n') {
			fprintf(stderr,
"Error parsing '%s': expected newline after command at line %d\n",
				filename, line);
			patch_free(patch);
			return RET_ERROR;
		}
		d++;
		line++;

		if (type != 'a' && number == 0) {
			fprintf(stderr,
"Error parsing '%s': missing number at line %d\n",
				filename, line);
			patch_free(patch);
			return RET_ERROR;
		}
		if (type != 'c' && type != 'd'  && number2 >= 0) {
			fprintf(stderr,
"Error parsing '%s': line range not allowed with %c at line %d\n",
				filename, (char)type, line);
			patch_free(patch);
			return RET_ERROR;
		}
		n = zNEW(struct modification);
		if (FAILEDTOALLOC(n)) {
			patch_free(patch);
			return RET_ERROR_OOM;
		}
		n->next = patch->modifications;
		if (n->next != NULL)
			n->next->previous = n;
		patch->modifications = n;

		p = d;
		if (type == 'd') {
			n->content = NULL;
			n->len = 0;
			n->newlinecount = 0;
		} else {
			int startline = line;

			l = p;
			while (l < e) {
				p = l;
				while (l < e && *l != '\n')
					l++;
				if (l >= e) {
					if (l == p + 1 && *p == '.') {
						/* that is also corrupted,
						 * but we can cure it */
						break;
					}
					fprintf(stderr,
"Error parsing '%s': ends in unterminated line. File most likely corrupted\n",
							filename);
					patch_free(patch);
					return RET_ERROR;
				}
				l++;
				if (p[0] == '.' && (p[1] == '\n' || p[1] == '\r'))
					break;
				line++;
			}
			if (p[0] != '.' || (l > p + 1 && p[1] != '\n' && p[1] != '\r')) {
					fprintf(stderr,
"Error parsing '%s': ends waiting for dot. File most likely corrupted\n",
							filename);
					patch_free(patch);
					return RET_ERROR;
			}
			n->content = d;
			n->len = p - d;
			n->newlinecount = line - startline;
			p = l;
			line++;
		}
		if (type == 'a') {
			/* appends appends after instead of before something: */
			n->oldlinestart = number + 1;
			n->oldlinecount = 0;
		} else if (type == 'i') {
			n->oldlinestart = number;
			n->oldlinecount = 0;
		} else {
			n->oldlinestart = number;
			if (number2 < 0)
				n->oldlinecount = 1;
			else
				n->oldlinecount = (number2 - number) + 1;
		}
		/* make sure things are in the order diff usually
		 * generates them, which makes line-calculation much easier: */
		if (n->next != NULL) {
			if (n->oldlinestart + n->oldlinecount
					> n->next->oldlinestart) {
				struct modification *first, *second;
				retvalue r;

				// TODO: it might be more efficient to
				// first store the different parts as different
				// patchsets and then combine...

				/* unlink and feed into patch merger */
				first = n->next;
				first->previous = NULL;
				second = n;
				n->next = NULL;
				n = NULL;
				r = combine_patches(&n, first, second);
				patch->modifications = n;
				if (RET_WAS_ERROR(r)) {
					patch_free(patch);
					return r;
				}
			}
		}
	}
	*patch_p = patch;
	return RET_OK;
}

static void modification_stripendlines(struct modification *m, int r) {
	int lines;
	const char *p;

	m->newlinecount -= r;
	lines = m->newlinecount;
	p = m->content;
	while (lines > 0) {
		while (*p != '\n')
			p++;
		p++;
		lines--;
	}
	assert ((size_t)(p - m->content) <= m->len);
	m->len = p - m->content;
}

static void modification_stripstartlines(struct modification *m, int r) {
	const char *p;

	m->newlinecount -= r;
	p = m->content;
	while (r > 0) {
		while (*p != '\n')
			p++;
		p++;
		r--;
	}
	assert ((size_t)(p - m->content) <= m->len);
	m->len -= p - m->content;
	m->content = p;
}

static inline void move_queue(struct modification **last_p, struct modification **result_p, struct modification **from_p) {
	struct modification *toadd, *last;

	/* remove from queue: */
	toadd = *from_p;
	*from_p = toadd->next;
	if (toadd->next != NULL) {
		toadd->next->previous = NULL;
		toadd->next = NULL;
	}

	/* if nothing yet, make it the first */
	if (*last_p == NULL) {
		*result_p = toadd;
		toadd->previous = NULL;
		*last_p = toadd;
		return;
	}

	last = *last_p;
	if (toadd->oldlinestart == last->oldlinestart + last->oldlinecount) {
		/* check if something can be combined: */
		if (toadd->newlinecount == 0) {
			last->oldlinecount += toadd->oldlinecount;
			free(toadd);
			return;
		}
		if (last->newlinecount == 0) {
			toadd->oldlinestart = last->oldlinestart;
			toadd->oldlinecount += last->oldlinecount;
			toadd->previous = last->previous;
			if (toadd->previous == NULL)
				*result_p = toadd;
			else
				toadd->previous->next = toadd;
			*last_p = toadd;
			free(last);
			return;
		}
		if (last->content + last->len == toadd->content) {
			last->oldlinecount += toadd->oldlinecount;
			last->newlinecount += toadd->newlinecount;
			last->len += toadd->len;
			free(toadd);
			return;
		}
	}
	toadd->previous = last;
	last->next = toadd;
	assert (last->oldlinestart + last->oldlinecount <= toadd->oldlinestart);
	*last_p = toadd;
	return;
}

/* this merges a set of modifications into an already existing stack,
 * modifying line numbers or even cutting away deleted/newly overwritten
 * stuff as necessary */
retvalue combine_patches(struct modification **result_p, /*@only@*/struct modification *first, /*@only@*/struct modification *second) {
	struct modification *p, *a, *result, *last;
	long lineofs;

	p = first;
	result = NULL;
	last = NULL;
	a = second;

	lineofs = 0;

	while (a != NULL) {
		/* modification totally before current one,
		 * so just add it before it */
		if (p == NULL || lineofs + a->oldlinestart + a->oldlinecount
				<= p->oldlinestart) {
			a->oldlinestart += lineofs;
			move_queue(&last, &result, &a);
			assert (p == NULL || p->oldlinestart >=
					last->oldlinestart + last->oldlinecount);
			continue;
		}
		/* modification to add after current head modification,
		 * so finalize head modification and update lineofs */
		if (lineofs + a->oldlinestart
				>= p->oldlinestart + p->newlinecount) {
			lineofs += p->oldlinecount - p->newlinecount;
			move_queue(&last, &result, &p);
			assert (lineofs + a->oldlinestart >=
					last->oldlinestart + last->oldlinecount);
			continue;
		}
		/* new modification removes everything the old one added: */
		if (lineofs + a->oldlinestart <= p->oldlinestart
			  && lineofs + a->oldlinestart + a->oldlinecount
				>= p->oldlinestart + p->newlinecount) {
			a->oldlinestart -= p->oldlinecount - p->newlinecount;
			a->oldlinecount += p->oldlinecount - p->newlinecount;
			lineofs += p->oldlinecount - p->newlinecount;
			p = modification_freehead(p);
			if (a->oldlinecount == 0 && a->newlinecount == 0) {
				/* a exactly cancels p */
				a = modification_freehead(a);
			}
			/* otherwise a is not yet finished,
			 * it might modify more */
			continue;
		}
		/* otherwise something overlaps, things get complicated here: */

		/* start of *a removes end of *p, so reduce *p: */
		if (lineofs + a->oldlinestart > p->oldlinestart &&
				lineofs + a->oldlinestart
				< p->oldlinestart + p->newlinecount &&
				lineofs + a->oldlinestart + a->oldlinecount
				>= p->oldlinestart + p->newlinecount) {
			int removedlines = p->oldlinestart + p->newlinecount
				- (lineofs + a->oldlinestart);

			/* finalize p as before */
			lineofs += p->oldlinecount - p->newlinecount;
			/* just telling a to delete less */
			a->oldlinestart += removedlines;
			a->oldlinecount -= removedlines;
			/* and p to add less */
			modification_stripendlines(p, removedlines);
			move_queue(&last, &result, &p);
			assert (lineofs + a->oldlinestart >=
					last->oldlinestart + last->oldlinecount);
			continue;
		}
		/* end of *a remove start of *p, so finalize *a and reduce *p */
		if (lineofs + a->oldlinestart <= p->oldlinestart &&
				lineofs + a->oldlinestart + a->oldlinecount
				> p->oldlinestart  &&
				lineofs + a->oldlinestart + a->oldlinecount
				< p->oldlinestart + p->newlinecount) {
			int removedlines =
				lineofs + a->oldlinestart + a->oldlinecount
				- p->oldlinestart;
			/* finalize *a with less lines deleted:*/
			a->oldlinestart += lineofs;
			a->oldlinecount -= removedlines;
			if (a->oldlinecount == 0 && a->newlinecount == 0) {
				/* a only removed something and this was hereby
				 * removed from p */
				a = modification_freehead(a);
			} else
				move_queue(&last, &result, &a);
			/* and reduce the number of lines of *p */
			assert (removedlines < p->newlinecount);
			modification_stripstartlines(p, removedlines);
			/* p->newlinecount got smaller,
			 * so less will be deleted later */
			lineofs -= removedlines;
			if (last != NULL) {
			assert (p->oldlinestart >=
				last->oldlinestart + last->oldlinecount);
			if (a != NULL)
				assert (lineofs + a->oldlinestart >=
				       last->oldlinestart + last->oldlinecount);
			}
			/* note that a->oldlinestart+a->oldlinecount+1
			 *        == p->oldlinestart */
			continue;
		}
		/* the most complex case left, a inside p, this
		 * needs p split in two */
		if (lineofs + a->oldlinestart > p->oldlinestart &&
				lineofs + a->oldlinestart + a->oldlinecount
				< p->oldlinestart + p->newlinecount) {
			struct modification *n;
			int removedlines = p->oldlinestart + p->newlinecount
				- (lineofs + a->oldlinestart);

			n = zNEW(struct modification);
			if (FAILEDTOALLOC(n)) {
				modification_freelist(result);
				modification_freelist(p);
				modification_freelist(a);
				return RET_ERROR_OOM;
			}
			*n = *p;
			/* all removing into the later p, so
			 * that later numbers fit */
			n->next = NULL;
			n->oldlinecount = 0;
			assert (removedlines < n->newlinecount);
			modification_stripendlines(n, removedlines);
			lineofs += n->oldlinecount - n->newlinecount;
			assert (lineofs+a->oldlinestart <= p->oldlinestart);
			move_queue(&last, &result, &n);
			assert (n == NULL);
			/* only remove this and let the rest of the
			 * code handle the other changes */
			modification_stripstartlines(p,
					p->newlinecount - removedlines);
			assert(p->newlinecount == removedlines);
			assert (lineofs + a->oldlinestart >=
					last->oldlinestart + last->oldlinecount);
			continue;
		}
		modification_freelist(result);
		modification_freelist(p);
		modification_freelist(a);
		fputs("Internal error in rred merging!\n", stderr);
		return RET_ERROR;
	}
	while (p != NULL) {
		move_queue(&last, &result, &p);
	}
	*result_p = result;
	return RET_OK;
}

retvalue patch_file(FILE *o, const char *source, const struct modification *patch) {
	FILE *i;
	int currentline, ignore, c;

	i = fopen(source, "r");
	if (i == NULL) {
		int e = errno;
		fprintf(stderr, "Error %d opening %s: %s\n",
				e, source, strerror(e));
		return RET_ERRNO(e);
	}
	assert (patch == NULL || patch->oldlinestart > 0);
	currentline = 1;
	do {
		while (patch != NULL && patch->oldlinestart == currentline) {
			fwrite(patch->content, patch->len, 1, o);
			ignore = patch->oldlinecount;
			patch = patch->next;
			while (ignore > 0) {
				do {
					c = getc(i);
				} while (c != '\n' && c != EOF);
				ignore--;
				currentline++;
			}
		}
		assert (patch == NULL || patch->oldlinestart >= currentline);
		while ((c = getc(i)) != '\n') {
			if (c == EOF) {
				if (patch != NULL) {
					fprintf(stderr,
"Error patching '%s', file shorter than expected by patches!\n",
						source);
					(void)fclose(i);
					return RET_ERROR;
				}
				break;
			}
			putc(c, o);
		}
		if (c == EOF)
			break;
		putc(c, o);
		currentline++;
	} while (1);
	if (ferror(i) != 0) {
		int e = errno;
		fprintf(stderr, "Error %d reading %s: %s\n",
				e, source, strerror(e));
		(void)fclose(i);
		return RET_ERRNO(e);
	}
	if (fclose(i) != 0) {
		int e = errno;
		fprintf(stderr, "Error %d reading %s: %s\n",
				e, source, strerror(e));
		return RET_ERRNO(e);
	}
	return RET_OK;
}

void modification_printaspatch(void *f, const struct modification *m, void write_func(const void *, size_t, void *)) {
	const struct modification *p, *q, *r;
	char line[30];
	int len;

	if (m == NULL)
		return;
	assert (m->previous == NULL);
	/* go to the end, as we have to print it backwards */
	p = m;
	while (p->next != NULL) {
		assert (p->next->previous == p);
		p = p->next;
	}
	/* then print, possibly merging things */
	while (p != NULL) {
		int start, oldcount, newcount;
		start = p->oldlinestart;
		oldcount = p->oldlinecount;
		newcount = p->newlinecount;

		if (p->next != NULL)
			assert (start + oldcount <= p->next->oldlinestart);

		r = p;
		for (q = p->previous ;
		     q != NULL && q->oldlinestart + q->oldlinecount == start ;
		     q = q->previous) {
			oldcount += q->oldlinecount;
			start = q->oldlinestart;
			newcount += q->newlinecount;
			r = q;
		}
		if (newcount == 0) {
			assert (oldcount > 0);
			if (oldcount == 1)
				len = snprintf(line, sizeof(line), "%dd\n",
						start);
			else
				len = snprintf(line, sizeof(line), "%d,%dd\n",
						start, start + oldcount - 1);
		} else {
			if (oldcount == 0)
				len = snprintf(line, sizeof(line), "%da\n",
						start - 1);
			else if (oldcount == 1)
				len = snprintf(line, sizeof(line), "%dc\n",
						start);
			else
				len = snprintf(line, sizeof(line), "%d,%dc\n",
						start, start + oldcount - 1);
		}
		assert (len < (int)sizeof(line));
		write_func(line, len, f);
		if (newcount != 0) {
			while (r != p->next) {
				if (r->len > 0)
					write_func(r->content, r->len, f);
				newcount -= r->newlinecount;
				r = r->next;
			}
			assert (newcount == 0);
			write_func(".\n", 2, f);
		}
		p = q;
	}
}

/* make sure a patch is not empty and does not only add lines at the start,
 * to work around some problems in apt */

retvalue modification_addstuff(const char *source, struct modification **patch_p, char **line_p) {
	struct modification **pp, *n, *m = NULL;
	char *line = NULL; size_t bufsize = 0;
	ssize_t got;
	FILE *i;
	long lineno = 0;

	pp = patch_p;
	/* check if this only adds things at the start and count how many */
	while (*pp != NULL) {
		m = *pp;
		if (m->oldlinecount > 0 || m->oldlinestart > 1) {
			*line_p = NULL;
			return RET_OK;
		}
		lineno += m->newlinecount;
		pp = &(*pp)->next;
	}
	/* not get the next line and claim it was changed */
	i = fopen(source, "r");
	if (i == NULL) {
		int e = errno;
		fprintf(stderr, "Error %d opening '%s': %s\n",
				e, source, strerror(e));
		return RET_ERRNO(e);
	}
	do {
		got = getline(&line, &bufsize, i);
	} while (got >= 0 && lineno-- > 0);
	if (got < 0) {
		int e = errno;

		/* You should have made sure the old file is not empty */
		fprintf(stderr, "Error %d reading '%s': %s\n",
				e, source, strerror(e));
		(void)fclose(i);
		return RET_ERRNO(e);
	}
	(void)fclose(i);

	n = NEW(struct modification);
	if (FAILEDTOALLOC(n))
		return RET_ERROR_OOM;
	*pp = n;
	n->next = NULL;
	n->previous = m;
	n->oldlinestart = 1;
	n->oldlinecount = 1;
	n->newlinecount = 1;
	n->len = got;
	n->content = line;
	*line_p = line;
	return RET_OK;
}

