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
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include "error.h"
// #include "rredpatch.h"

struct modification {
	/* next item in the list (sorted by oldlinestart) */
	struct modification *next;
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
};

static void modification_freelist(/*@only@*/struct modification *p) {
	while( p != NULL ) {
		struct modification *m = p;
		p = m->next;
		free(m);
	}
}

static struct modification *modification_freehead(/*@only@*/struct modification *p) {
	struct modification *m = p->next;
	free(p);
	return m;
}

static void patch_free(/*@only@*/struct rred_patch *p) {
	if( p->data != NULL )
		(void)munmap(p->data, p->len);
	if( p->fd >= 0 )
		(void)close(p->fd);
	modification_freelist(p->modifications);
	free(p);
}

static retvalue load_patch(const char *filename, off_t length, struct rred_patch **patch_p) {
	int i;
	struct rred_patch *patch;
	const char *p, *e, *d, *l;
	int number, number2, line;
	char type;
	struct modification *n;
	struct stat statbuf;

	patch = calloc(1, sizeof(struct rred_patch));
	if( FAILEDTOALLOC(patch) )
		return RET_ERROR_OOM;
	patch->fd = open(filename, O_NOCTTY|O_RDONLY);
	if( patch->fd < 0 ) {
		int err = errno;
		fprintf(stderr,
"Error %d opening '%s' for reading: %s\n", err, filename, strerror(err));
		patch_free(patch);
		return RET_ERRNO(err);
	}
	i = fstat(patch->fd, &statbuf);
	if( i != 0 ) {
		int err = errno;
		fprintf(stderr,
"Error %d retrieving length of '%s': %s\n", err, filename, strerror(err));
		patch_free(patch);
		return RET_ERRNO(err);
	}
	if( length == -1 )
		length = statbuf.st_size;
	if( statbuf.st_size != length ) {
		int err = errno;
		fprintf(stderr,
"Unexpected size of '%s': expected %lld, got %lld\n", filename,
			(long long)length, (long long)statbuf.st_size);
		patch_free(patch);
		return RET_ERRNO(err);
	}
	patch->len = length;
	patch->data = mmap(NULL, patch->len, PROT_READ, MAP_PRIVATE,
			patch->fd, 0);
	if( patch->data == MAP_FAILED ) {
		int err = errno;
		fprintf(stderr,
"Error %d mapping '%s' into memory: %s\n", err, filename, strerror(err));
		patch_free(patch);
		return RET_ERRNO(err);
	}
	p = patch->data;
	e = p + patch->len;
	line = 1;
	while( p < e ) {
		/* <number>,<number>(c|d)\n or <number>(a|i|c|d) */
		d = p;
		number = 0; number2 = -1;
		while( d < e && *d >= '0' && *d <= '9' ) {
			number = (*d - '0') + 10 * number;
			d++;
		}
		if( d > p && d < e && *d == ',' ) {
			d++;
			number2 = 0;
			while( d < e && *d >= '0' && *d <= '9' ) {
				number2 = (*d - '0') + 10 * number2;
				d++;
			}
			if( number2 < number ) {
				fprintf(stderr,
"Error parsing '%s': malformed range (2nd number smaller than 1s) at line %d\n",
					filename, line);
				patch_free(patch);
				return RET_ERROR;
			}
		}
		if( d >= e || (*d != 'c' && *d != 'i' && *d != 'a' && *d != 'd') ) {
			fprintf(stderr,
"Error parsing '%s': expected rule (c,i,a or d) at line %d\n",
				filename, line);
			patch_free(patch);
			return RET_ERROR;
		}
		type = *d;
		d++;
		while( d < e && *d == '\r' )
			d++;
		if( d >= e || *d != '\n' ) {
			fprintf(stderr,
"Error parsing '%s': expected newline after command at line %d\n",
				filename, line);
			patch_free(patch);
			return RET_ERROR;
		}
		d++;
		line++;

		if( type != 'a' && number == 0 ) {
			fprintf(stderr,
"Error parsing '%s': missing number at line %d\n",
				filename, line);
			patch_free(patch);
			return RET_ERROR;
		}
		if( type != 'c' && type != 'd'  && number2 >= 0 ) {
			fprintf(stderr,
"Error parsing '%s': line range not allowed with %c at line %d\n",
				filename, (char)type, line);
			patch_free(patch);
			return RET_ERROR;
		}
		n = calloc(1, sizeof(struct modification));
		if( FAILEDTOALLOC(n) ) {
			patch_free(patch);
			return RET_ERROR_OOM;
		}
		n->next = patch->modifications;
		patch->modifications = n;

		p = d;
		if( type == 'd') {
			n->content = NULL;
			n->len = 0;
			n->newlinecount = 0;
		} else {
			int startline = line;

			l = p;
			while( l < e ) {
				p = l;
				while( l < e && *l != '\n' )
					l++;
				if( l >= e ) {
					if( l == p + 1 && *p == '.' ) {
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
				if( p[0] == '.' && (p[1] == '\n' || p[1] == '\r') )
					break;
				line++;
			}
			if( p[0] != '.' || ( l > p + 1 && p[1] != '\n' && p[1] != '\r' )) {
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
		if( type == 'a' ) {
			/* appends appends after instead of before something: */
			n->oldlinestart = number + 1;
			n->oldlinecount = 0;
		} else if( type == 'i' ) {
			n->oldlinestart = number;
			n->oldlinecount = 0;
		} else {
			n->oldlinestart = number;
			if( number2 < 0 )
				n->oldlinecount = 1;
			else
				n->oldlinecount = (number2 - number) + 1;
		}
		/* make sure things are in the order diff usually
		 * generates them, which makes line-calculation much easier: */
		if( n->next != NULL ) {
			// TODO: theoretically, that could be healed by
			// changing the numbers when this item is moving to
			// the later code. But how test this as diff uses
			// the sane order?
			if( n->oldlinestart + n->oldlinecount
					> n->next->oldlinestart ) {
				fprintf(stderr,
"Error using '%s': wrong order of edit-commands\n",
						filename);
				patch_free(patch);
				return RET_ERROR;
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
	while( lines > 0 ) {
		while( *p != '\n')
			p++;
		p++;
		lines--;
	}
	assert( p - m->content <= m->len );
	m->len = p - m->content;
}

static void modification_stripstartlines(struct modification *m, int r) {
	const char *p;

	m->newlinecount -= r;
	p = m->content;
	while( r > 0 ) {
		while( *p != '\n')
			p++;
		p++;
		r--;
	}
	assert( p - m->content <= m->len );
	m->len -= p - m->content;
	m->content = p;
}

/* this merges a set of modifications into an already existing stack,
 * modifying line numbers or even cutting away deleted/newly overwritten
 * stuff as necessary */
static retvalue combine_patches(struct modification **into_p, /*@only@*/struct modification *first, /*@only@*/struct modification *second) {
	struct modification **pp, *p, *a, *result;
	long lineofs;

	p = first;
	result = NULL;
	pp = &result;
	a = second;

	lineofs = 0;

	while( a != NULL ) {
		if( p == NULL ) {
			*pp = a;
			while( a != NULL ) {
				a->oldlinestart += lineofs;
				pp = &a->next;
				a = a->next;
			}
			break;
		}
		/* modification totally before current one,
		 * so just add it before it */
		if( lineofs + a->oldlinestart + a->oldlinecount
				<= p->oldlinestart ) {
			a->oldlinestart += lineofs;
			*pp = a;
			a = a->next;
			*(pp = &(*pp)->next) = NULL;
			continue;
		}
		/* modification to add after current head modification,
		 * so finalize head modification and update lineofs */
		if( lineofs + a->oldlinestart
			  	>= p->oldlinestart + p->newlinecount ) {
			lineofs += p->oldlinecount - p->newlinecount;
			*pp = p;
			p = p->next;
			*(pp = &(*pp)->next) = NULL;
			continue;
		}
		/* new modification removes everything the old one added: */
		if( lineofs + a->oldlinestart <= p->oldlinestart
			  && lineofs + a->oldlinestart + a->oldlinecount
			  	>= p->oldlinestart + p->newlinecount ) {
			a->oldlinestart += lineofs;
			lineofs += p->oldlinecount - p->newlinecount;
			a->oldlinecount += p->oldlinecount - p->newlinecount;
			p = modification_freehead(p);
			/* we cannot finalize a here, as it might still
			 * eat more p (in full or partial)... */
			continue;
		}
		/* otherwise something overlaps, things get complicated here: */

		/* start of *a removes end of *p, so reduce *p: */
		if( lineofs + a->oldlinestart > p->oldlinestart &&
				lineofs + a->oldlinestart
				< p->oldlinestart + p->newlinecount &&
				lineofs + a->oldlinestart + a->oldlinecount
				>= p->oldlinestart + p->newlinecount ) {
			int removedlines = p->oldlinestart + p->newlinecount
				- (lineofs + a->oldlinestart);

			/* finalize p as before */
			lineofs += p->oldlinecount - p->newlinecount;
			/* just telling a to delete less */
			a->oldlinestart += removedlines;
			a->oldlinecount -= removedlines;
			/* and p to add less */
			modification_stripendlines(p, removedlines);
			*pp = p;
			p = p->next;
			*(pp = &(*pp)->next) = NULL;
			continue;
		}
		/* end of *a remove start of *p, so finalize *a and reduce *p */
		if( lineofs + a->oldlinestart <= p->oldlinestart &&
				lineofs + a->oldlinestart + a->oldlinecount
				> p->oldlinestart  &&
				lineofs + a->oldlinestart + a->oldlinecount
				< p->oldlinestart + p->newlinecount ) {
			int removedlines =
				lineofs + a->oldlinestart + a->oldlinecount
				- p->oldlinestart;
			/* finalize *a with less lines deleted:*/
			a->oldlinestart += lineofs;
			a->oldlinecount -= removedlines;
			*pp = a;
			a = a->next;
			*(pp = &(*pp)->next) = NULL;
			/* and reduce the number of lines of *p */
			modification_stripstartlines(p, removedlines);
			/* note that a->oldlinestart+a->oldlinecount+1
			 *        == p->oldlinestart */
			continue;
		}
		/* the most complex case left, a inside p, this
		 * needs p split in two */
		if( lineofs + a->oldlinestart > p->oldlinestart &&
				lineofs + a->oldlinestart + a->oldlinecount
				< p->oldlinestart + p->newlinecount ) {
			struct modification *n;
			int removedlines = p->oldlinestart + p->newlinecount
				- (lineofs + a->oldlinestart);

			n = calloc(1, sizeof(struct modification));
			if( FAILEDTOALLOC(n) ) {
				modification_freelist(result);
				modification_freelist(p);
				modification_freelist(a);
				return RET_ERROR_OOM;
			}
			*n = *p;
			/* all removing into the later pater, so
			 * that later numbers fit */
			n->oldlinecount = 0;
			modification_stripendlines(n,
					n->newlinecount - removedlines);
			assert(n->newlinecount == removedlines);
			lineofs += n->oldlinecount - n->newlinecount;
			assert( lineofs+a->oldlinestart <= p->oldlinestart);
			*pp = n;
			*(pp = &(*pp)->next) = NULL;
			/* only remove this and let the rest of the
			 * code handle the other changes */
			modification_stripstartlines(p, removedlines);
		}
		*into_p = NULL;
		modification_freelist(result);
		modification_freelist(p);
		modification_freelist(a);
		fputs("Internal error in rred merging!\n", stderr);
		return RET_ERROR;
	}
	*pp = p;
	*into_p = result;
	return RET_OK;
}

static retvalue patch_file(const char *destination, const char *source, const struct modification *patch) {
	FILE *i, *o;
	int currentline, ignore, c;
	/* run through the file, copying unless there is a a modification,
	 * things get complex with multiple patch files, as every patch may see
	 * a different current line number */

	i = fopen(source, "r");
	if( i == NULL ) {
		perror("error opening\n");
		return RET_ERROR;
	}
	o = fopen(destination, "w");
	if( o == NULL ) {
		perror("error creating\n");
		return RET_ERROR;
	}
	while( patch != NULL && patch->oldlinestart == 0 ) {
		fwrite(patch->content, patch->len, 1, o);
		assert( patch->oldlinecount == 0);
		patch = patch->next;
	}
	currentline = 1;
	do {
		while( patch != NULL && patch->oldlinestart == currentline ) {
			fwrite(patch->content, patch->len, 1, o);
			ignore = patch->oldlinecount;
			patch = patch->next;
			while( ignore > 0 ) {
				do {
					c = getc(i);
				} while( c != '\n' && c != EOF);
				ignore--;
				currentline++;
			}
		}
		assert( patch == NULL || patch->oldlinestart >= currentline );
		while( (c = getc(i)) != '\n' ) {
			if( c == EOF ) {
				if( patch != NULL ) {
					fprintf(stderr,
"Error patching '%s', file shorter than expected by patches!\n",
						source);
					return RET_ERROR;
				}
				return RET_OK;
			}
			putc(c, o);
		}
		putc(c, o);
		currentline++;
	} while (1);
	// TODO: check errors
	fclose(i);
	fclose(o);
	return RET_OK;
}

static void modification_print(const struct modification *m) {
	while( m != NULL ) {
		printf("mod %d+%d to %d:\n", m->oldlinestart, m->oldlinecount, m->newlinecount);
		fwrite(m->content, m->len, 1, stdout);
		m = m->next;
	}
}
/*
int main() {
	struct rred_patch *p1, *p2, *p3;
	struct modification *m;
	retvalue r;

	r = load_patch("patch1", -1, &p1);
	if( RET_WAS_ERROR(r) )
		return 1;
	r = load_patch("patch2", -1, &p2);
	if( RET_WAS_ERROR(r) )
		return 2;
	r = load_patch("patch3", -1, &p3);
	if( RET_WAS_ERROR(r) )
		return 3;
	printf("p1:\n");
	modification_print(p1->modifications);
	printf("p2:\n");
	modification_print(p2->modifications);
	printf("p3:\n");
	modification_print(p3->modifications);
	printf("combine1:\n");
	r = combine_patches(&m, p1->modifications, p2->modifications);
	p1->modifications = NULL; p2->modifications = NULL;
	if( RET_WAS_ERROR(r) )
		return 4;
	modification_print(m);
	printf("combine2:\n");
	r = combine_patches(&m, m, p3->modifications);
	p3->modifications = NULL;
	if( RET_WAS_ERROR(r) )
		return 5;
	modification_print(m);
	r = patch_file("destination", "source", m);
	if( RET_WAS_ERROR(r) )
		return 6;
	patch_free(p1);
	patch_free(p2);
	patch_free(p3);
	modification_freelist(m);
	return 0;
}
*/
