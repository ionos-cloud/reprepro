/*  This file is part of "reprepro"
 *  Copyright (C) 2005 Bernhard R. Link
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
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "error.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "configparser.h"
#include "filterlist.h"

struct filterlistfile {
	size_t reference_count;

	char *filename;
	size_t filename_len;

	/*@owned@*//*@null@*/
	struct filterlistitem *root;
	/*@dependent@*//*@null@*/
	const struct filterlistitem *last;

	/*@owned@*//*@null@*/
	struct filterlistfile *next;
} *listfiles = NULL;

struct filterlistitem {
	/*@owned@*//*@null@*/
	struct filterlistitem *next;
	char *packagename;
	char *version;
	enum filterlisttype what;
};

static void filterlistitems_free(/*@null@*//*@only@*/struct filterlistitem *list) {
	while (list != NULL) {
		struct filterlistitem *next = list->next;
		free(list->version);
		free(list->packagename);
		free(list);
		list = next;
	}
}

static void filterlistfile_unlock(struct filterlistfile *list) {
	assert (list != NULL);

	if (list->reference_count <= 1) {
		struct filterlistfile **p = &listfiles;

		assert (list->reference_count == 1);
		if (list->reference_count == 0)
			return;

		while (*p != NULL && *p != list)
			p = &(*p)->next;
		assert (p != NULL);
		if (*p == list) {
			*p = list->next;
			filterlistitems_free(list->root);
			free(list->filename);
			free(list);
		}
	} else
		list->reference_count--;
}

static inline retvalue filterlistfile_parse(struct filterlistfile *n, const char *filename, FILE *f) {
	char *lineend, *namestart, *nameend, *what, *version;
	int cmp;
	enum filterlisttype type;
	struct filterlistitem *h;
	char line[1001];
	int lineno = 0;
	struct filterlistitem **last = &n->root;

	while (fgets(line, 1000, f) != NULL) {
		lineno++;
		lineend = strchr(line, '\n');
		if (lineend == NULL) {
			fprintf(stderr, "Overlong or unterminated line in '%s'!\n", filename);
			return RET_ERROR;
		}
		while (lineend >= line && xisspace(*lineend))
			*(lineend--) = '\0';
		/* Ignore line only containing whitespace */
		if (line[0] == '\0')
			continue;
		namestart = line;
		while (*namestart != '\0' && xisspace(*namestart))
			namestart++;
		nameend=namestart;
		while (*nameend != '\0' && !xisspace(*nameend))
			nameend++;
		what = nameend;
		while (*what != '\0' && xisspace(*what))
			*(what++)='\0';
		if (*what == '\0') {
			fprintf(stderr,
"Malformed line in '%s': %d!\n", filename, lineno);
			return RET_ERROR;
		}
		version = NULL;
		if (strcmp(what, "install") == 0) {
			type = flt_install;
		} else if (strcmp(what, "deinstall") == 0) {
			type = flt_deinstall;
		} else if (strcmp(what, "purge") == 0) {
			type = flt_purge;
		} else if (strcmp(what, "hold") == 0) {
			type = flt_hold;
		} else if (strcmp(what, "supersede") == 0) {
			type = flt_supersede;
		} else if (strcmp(what, "upgradeonly") == 0) {
			type = flt_upgradeonly;
		} else if (strcmp(what, "warning") == 0) {
			type = flt_warning;
		} else if (strcmp(what, "error") == 0) {
			type = flt_error;
		} else if (what[0] == '=') {
			what++;
			while (*what != '\0' && xisspace(*what))
				what++;
			version = what;
			if (*version == '\0') {
				fprintf(stderr,
"Malformed line %d in '%s': missing version after '='!\n",
						lineno, filename);
				return RET_ERROR;
			}
			while (*what != '\0' && !xisspace(*what))
				what++;
			while (*what != '\0' && xisspace(*what))
				*(what++) = '\0';
			if (*what != '\0') {
				fprintf(stderr,
"Malformed line %d in '%s': space in version!\n",
						lineno, filename);
				return RET_ERROR;
			}
			type = flt_install;
		} else {
			fprintf(stderr,
"Unknown status in '%s':%d: '%s'!\n", filename, lineno, what);
			return RET_ERROR;
		}
		if (*last == NULL || strcmp(namestart, (*last)->packagename) < 0)
			last = &n->root;
		cmp = -1;
		while (*last != NULL &&
				(cmp=strcmp(namestart, (*last)->packagename)) > 0)
			last = &((*last)->next);
		if (cmp == 0) {
			fprintf(stderr,
"Two lines describing '%s' in '%s'!\n", namestart, filename);
			return RET_ERROR;
		}
		h = zNEW(struct filterlistitem);
		if (FAILEDTOALLOC(h)) {
			return RET_ERROR_OOM;
		}
		h->next = *last;
		*last = h;
		h->what = type;
		h->packagename = strdup(namestart);
		if (FAILEDTOALLOC(h->packagename)) {
			return RET_ERROR_OOM;
		}
		if (version == NULL)
			h->version = NULL;
		else {
			h->version = strdup(version);
			if (FAILEDTOALLOC(h->version))
				return RET_ERROR_OOM;
		}
	}
	n->last = *last;
	return RET_OK;

}

static inline retvalue filterlistfile_read(struct filterlistfile *n, const char *filename) {
	FILE *f;
	retvalue r;

	f = fopen(filename, "r");
	if (f == NULL) {
		fprintf(stderr, "Cannot open %s for reading: %s!\n",
				filename, strerror(errno));
		return RET_ERROR;
	}
	r = filterlistfile_parse(n, filename, f);

	// Can this return an yet unseen error? was read-only..
	(void)fclose(f);
	return r;
}

static inline retvalue filterlistfile_getl(const char *filename, size_t len, struct filterlistfile **list) {
	struct filterlistfile *p;
	retvalue r;

	for (p = listfiles ; p != NULL ; p = p->next) {
		if (p->filename_len == len &&
				strncmp(p->filename, filename, len) == 0) {
			p->reference_count++;
			*list = p;
			return RET_OK;
		}
	}
	p = zNEW(struct filterlistfile);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	p->reference_count = 1;
	p->filename = strndup(filename, len);
	p->filename_len = len;
	if (FAILEDTOALLOC(p->filename)) {
		free(p);
		return RET_ERROR_OOM;
	}
	char *fullfilename = configfile_expandname(p->filename, NULL);
	if (FAILEDTOALLOC(fullfilename))
		r = RET_ERROR_OOM;
	else {
		r = filterlistfile_read(p, fullfilename);
		free(fullfilename);
	}

	if (RET_IS_OK(r)) {
		p->next = listfiles;
		listfiles = p;
		*list = p;
	} else {
		filterlistitems_free(p->root);
		free(p->filename);
		free(p);
	}
	return r;
}

static inline retvalue filterlistfile_get(/*@only@*/char *filename, /*@out@*/struct filterlistfile **list) {
	struct filterlistfile *p;
	retvalue r;
	size_t len = strlen(filename);

	for (p = listfiles ; p != NULL ; p = p->next) {
		if (p->filename_len == len &&
				strncmp(p->filename, filename, len) == 0) {
			p->reference_count++;
			*list = p;
			free(filename);
			return RET_OK;
		}
	}
	p = zNEW(struct filterlistfile);
	if (FAILEDTOALLOC(p)) {
		free(filename);
		return RET_ERROR_OOM;
	}
	p->reference_count = 1;
	p->filename = filename;
	p->filename_len = len;
	if (FAILEDTOALLOC(p->filename)) {
		free(p);
		return RET_ERROR_OOM;
	}
	char *fullfilename = configfile_expandname(p->filename, NULL);
	if (FAILEDTOALLOC(fullfilename))
		r = RET_ERROR_OOM;
	else {
		r = filterlistfile_read(p, fullfilename);
		free(fullfilename);
	}

	if (RET_IS_OK(r)) {
		p->next = listfiles;
		listfiles = p;
		*list = p;
	} else {
		filterlistitems_free(p->root);
		free(p->filename);
		free(p);
	}
	return r;
}

void filterlist_release(struct filterlist *list) {
	size_t i;

	assert(list != NULL);

	if (list->files != NULL) {
		for (i = 0 ; i < list->count ; i++)
			filterlistfile_unlock(list->files[i]);
		free(list->files);
		list->files = NULL;
	} else {
		assert (list->count == 0);
	}
}

static const struct constant filterlisttype_listtypes[] = {
	{"install",	(int)flt_install},
	{"hold",	(int)flt_hold},
	{"supersede",	(int)flt_supersede},
	{"deinstall",	(int)flt_deinstall},
	{"purge",	(int)flt_purge},
	{"upgradeonly",	(int)flt_upgradeonly},
	{"warning",	(int)flt_warning},
	{"error",	(int)flt_error},
	{NULL, 0}
};

retvalue filterlist_load(struct filterlist *list, struct configiterator *iter) {
	enum filterlisttype defaulttype;
	size_t count;
	struct filterlistfile **files;
	retvalue r;
	char *filename;

	r = config_getenum(iter, filterlisttype, listtypes, &defaulttype);
	if (r == RET_NOTHING || r == RET_ERROR_UNKNOWNFIELD) {
		fprintf(stderr,
"Error parsing %s, line %u, column %u: Expected default action as first argument to FilterList: (one of install, purge, hold, ...)\n",
				config_filename(iter),
				config_markerline(iter),
				config_markercolumn(iter));
		return RET_ERROR;
	}
	if (RET_WAS_ERROR(r))
		return r;

	count = 0;
	files = NULL;
	while ((r = config_getword(iter, &filename)) != RET_NOTHING) {
		struct filterlistfile **n;

		n = realloc(files, (count+1)*
				sizeof(struct filterlistfile *));
		if (FAILEDTOALLOC(n)) {
			free(filename);
			r = RET_ERROR_OOM;
		} else {
			n[count] = NULL;
			files = n;
			// TODO: make filename only
			r = filterlistfile_get(filename, &files[count]);
			if (RET_IS_OK(r))
				count++;
		}
		if (RET_WAS_ERROR(r)) {
			while (count > 0) {
				count--;
				filterlistfile_unlock(files[count]);
			}
			free(files);
			return r;
		}
	}
	list->count = count;
	list->files = files;
	list->defaulttype = defaulttype;
	list->set = true;
	return RET_OK;
}

static inline bool find(const char *name, /*@null@*/struct filterlistfile *list) {
	int cmp;
	/*@dependent@*/const struct filterlistitem *last = list->last;

	assert (last != NULL);

	if (last->next != NULL) {
		cmp = strcmp(name, last->next->packagename);
		if (cmp == 0) {
			list->last = last->next;
			return true;
		}
	}
	if (last->next == NULL || cmp < 0) {
		cmp = strcmp(name, last->packagename);
		if (cmp == 0) {
			return true;
		} else if (cmp > 0)
			return false;
		last = list->root;
		cmp = strcmp(name, last->packagename);
		if (cmp == 0) {
			list->last = list->root;
			return true;
		} else if (cmp < 0)
			return false;
	}
	/* now we are after last */
	while (last->next != NULL) {
		cmp = strcmp(name, last->next->packagename);
		if (cmp == 0) {
			list->last = last->next;
			return true;
		}
		if (cmp < 0) {
			list->last = last;
			return false;
		}
		last = last->next;
	}
	list->last = last;
	return false;
}

enum filterlisttype filterlist_find(const char *name, const char *version, const struct filterlist *list) {
	size_t i;
	for (i = 0 ; i < list->count ; i++) {
		if (list->files[i]->root == NULL)
			continue;
		if (!find(name, list->files[i]))
			continue;
		if (list->files[i]->last->version == NULL)
			return list->files[i]->last->what;
		if (strcmp(list->files[i]->last->version, version) == 0)
			return list->files[i]->last->what;
	}
	return list->defaulttype;
}

struct filterlist cmdline_bin_filter = {
	.count = 0,
	.files = NULL,
	/* as long as nothing added, this does not change anything.
	 * Once something is added, that will be auto_hold */
	.defaulttype = flt_unchanged,
	.set = false,
};
struct filterlist cmdline_src_filter = {
	.count = 0,
	.files = NULL,
	/* as long as nothing added, this does not change anything.
	 * Once something is added, that will be auto_hold */
	.defaulttype = flt_unchanged,
	.set = false,
};

static retvalue filterlist_cmdline_init(struct filterlist *l) {
	if (l->count == 0) {
		l->files = nzNEW(2, struct filterlistfile *);
		if (FAILEDTOALLOC(l->files))
			return RET_ERROR_OOM;
		l->files[0] = zNEW(struct filterlistfile);
		if (FAILEDTOALLOC(l->files[0]))
			return RET_ERROR_OOM;
		l->files[0]->reference_count = 1;
		l->count = 1;
	}
	return RET_OK;
}

retvalue filterlist_cmdline_add_file(bool src, const char *filename) {
	retvalue r;
	struct filterlist *l = src ? &cmdline_src_filter : &cmdline_bin_filter;
	char *name;

	r = filterlist_cmdline_init(l);
	if (RET_WAS_ERROR(r))
		return r;
	l->set = true;
	l->defaulttype = flt_auto_hold;

	if (strcmp(filename, "-") == 0)
		filename = "/dev/stdin";
	name = strdup(filename);
	if (FAILEDTOALLOC(name))
		return RET_ERROR_OOM;
	if (l->count > 1) {
		struct filterlistfile **n;

		n = realloc(l->files, (l->count + 1) *
				sizeof(struct filterlistfile *));
		if (FAILEDTOALLOC(n)) {
			free(name);
			return RET_ERROR_OOM;
		}
		n[l->count++] = NULL;
		l->files = n;
	} else {
		/* already allocated in _init */
		assert (l->count == 1);
		l->count++;
	}

	return filterlistfile_get(name, &l->files[l->count - 1]);
}

retvalue filterlist_cmdline_add_pkg(bool src, const char *package) {
	retvalue r;
	enum filterlisttype what;
	struct filterlist *l = src ? &cmdline_src_filter : &cmdline_bin_filter;
	struct filterlistfile *f;
	struct filterlistitem **p, *h;
	char *name, *version;
	const char *c;
	int cmp;

	r = filterlist_cmdline_init(l);
	if (RET_WAS_ERROR(r))
		return r;
	l->set = true;
	l->defaulttype = flt_auto_hold;

	c = strchr(package, '=');
	if (c != NULL) {
		what = flt_install;
		name = strndup(package, c - package);
		if (FAILEDTOALLOC(name))
			return RET_ERROR_OOM;
		version = strdup(c + 1);
		if (FAILEDTOALLOC(version)) {
			free(name);
			return RET_ERROR_OOM;
		}
	} else {
		version = NULL;
		c = strchr(package, ':');
		if (c == NULL) {
			what = flt_install;
			name = strndup(package, c - package);
			if (FAILEDTOALLOC(name))
				return RET_ERROR_OOM;
		} else {
			const struct constant *t = filterlisttype_listtypes;
			while (t->name != NULL) {
				if (strcmp(c + 1, t->name) == 0) {
					what = t->value;
					break;
				}
				t++;
			}
			if (t->name == NULL) {
				fprintf(stderr,
"Error: unknown filter-outcome '%s' (expected 'install' or ...)\n",
						c + 1);
				return RET_ERROR;
			}

		}
		name = strndup(package, c - package);
		if (FAILEDTOALLOC(name))
			return RET_ERROR_OOM;
	}
	f = l->files[0];
	assert (f != NULL);
	p = &f->root;
	cmp = -1;
	while (*p != NULL && (cmp = strcmp(name, (*p)->packagename)) > 0)
		p = &((*p)->next);
	if (cmp == 0) {
		fprintf(stderr,
"Package in command line filter two times: '%s'\n",
				name);
		free(name);
		free(version);
		return RET_ERROR;
	}
	h = zNEW(struct filterlistitem);
	if (FAILEDTOALLOC(h)) {
		free(name);
		free(version);
		return RET_ERROR_OOM;
	}
	h->next = *p;
	*p = h;
	h->what = what;
	h->packagename = name;
	h->version = version;
	f->last = h;
	return RET_OK;
}
