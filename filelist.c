/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007,2016 Bernhard R. Link
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

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "error.h"
#include "database_p.h"
#include "files.h"
#include "chunks.h"
#include "package.h"
#include "debfile.h"
#include "filelist.h"

struct filelist_package {
	struct filelist_package *next;
	char name[];
};

struct dirlist;
struct filelist {
	struct filelist *nextl;
	struct filelist *nextr;
	int balance;
	char *name;
	size_t count;
	const char *packages[];
};
struct dirlist {
	struct dirlist *nextl;
	struct dirlist *nextr;
	int balance;
	/*@dependant@*/ struct dirlist *parent;
	struct dirlist *subdirs;
	struct filelist *files;
	/*@dependant@*/struct filelist *lastfile;
	size_t len;
	char name[];
};

struct filelist_list {
	struct dirlist *root;
	struct filelist_package *packages;
};

retvalue filelist_init(struct filelist_list **list) {
	struct filelist_list *filelist;

	filelist = zNEW(struct filelist_list);
	if (FAILEDTOALLOC(filelist))
		return RET_ERROR_OOM;
	filelist->root = zNEW(struct dirlist);
	if (FAILEDTOALLOC(filelist->root)) {
		free(filelist);
		return RET_ERROR_OOM;
	}
	*list = filelist;
	return RET_OK;
};
static void files_free(/*@only@*/struct filelist *list) {
	if (list == NULL)
		return;
	files_free(list->nextl);
	files_free(list->nextr);
	free(list->name);
	free(list);
}
static void dirlist_free(/*@only@*/struct dirlist *list) {
	if (list == NULL)
		return;
	files_free(list->files);
	dirlist_free(list->subdirs);
	dirlist_free(list->nextl);
	dirlist_free(list->nextr);
	free(list);
}
void filelist_free(struct filelist_list *list) {

	if (list == NULL)
		return;
	dirlist_free(list->root);
	while (list->packages != NULL) {
		struct filelist_package *package = list->packages;
		list->packages = package->next;
		free(package);
	}
	free(list);
};

static retvalue filelist_newpackage(struct filelist_list *filelist, const char *name, const char *section, const struct filelist_package **pkg) {
	struct filelist_package *p;
	size_t name_len = strlen(name);
	size_t section_len = strlen(section);

	p = malloc(sizeof(struct filelist_package)+name_len+section_len+2);
	if (FAILEDTOALLOC(p))
		return RET_ERROR_OOM;
	p->next = filelist->packages;
	memcpy(p->name, section, section_len);
	p->name[section_len] = '/';
	memcpy(p->name+section_len+1, name, name_len+1);
	filelist->packages = p;
	*pkg = p;
	return RET_OK;
};

static bool findfile(struct dirlist *parent, const char *packagename, const char *basefilename, size_t namelen) {
	struct filelist *file, *n, *last;
	struct filelist **stack[128];
	int stackpointer = 0;

	stack[stackpointer++] = &parent->files;
	file = parent->files;

	while (file != NULL) {
		int c = strncmp(basefilename, file->name, namelen);
		if (c == 0 && file->name[namelen] == '\0') {
			n = realloc(file, sizeof(struct filelist)+
					(file->count+1)*sizeof(const char*));
			if (n == NULL)
				return false;
			n->packages[n->count++] = packagename;
			*(stack[--stackpointer]) = n;
			return true;
		} else if (c > 0) {
			stack[stackpointer++] = &file->nextr;
			file = file->nextr;
		} else  {
			stack[stackpointer++] = &file->nextl;
			file = file->nextl;
		}
	}
	n = malloc(sizeof(struct filelist)+sizeof(const char*));
	if (FAILEDTOALLOC(n))
		return false;
	n->name = strndup(basefilename, namelen);
	n->nextl = NULL;
	n->nextr = NULL;
	n->balance = 0;
	n->count = 1;
	n->packages[0] = packagename;
	if (FAILEDTOALLOC(n->name)) {
		free(n);
		return false;
	}
	*(stack[--stackpointer]) = n;
	while (stackpointer > 0) {
		file = *(stack[--stackpointer]);
		if (file->nextl == n) {
			file->balance--;
			if (file->balance > -1)
				break;
			if (file->balance == -1) {
				n = file;
				continue;
			}
			if (n->balance == -1) {
				file->nextl = n->nextr;
				file->balance = 0;
				n->nextr = file;
				n->balance = 0;
				*(stack[stackpointer]) = n;
				break;
			} else {
				last = n->nextr;
				file->nextl = last->nextr;
				*(stack[stackpointer]) = last;
				last->nextr = file;
				n->nextr = last->nextl;
				last->nextl = n;
				if (last->balance == 0) {
					file->balance = 0;
					n->balance = 0;
				} else if (last->balance < 0) {
					file->balance = 1;
					n->balance = 0;
				} else {
					file->balance = 0;
					n->balance = -1;
				}
				last->balance = 0;
				break;
			}
		} else {
			file->balance++;
			if (file->balance < 1)
				break;
			if (file->balance == 1) {
				n = file;
				continue;
			}
			if (n->balance == 1) {
				file->nextr = n->nextl;
				file->balance = 0;
				n->nextl = file;
				n->balance = 0;
				*(stack[stackpointer]) = n;
				break;
			} else {
				last = n->nextl;
				file->nextr = last->nextl;
				*(stack[stackpointer]) = last;
				last->nextl = file;
				n->nextl = last->nextr;
				last->nextr = n;
				if (last->balance == 0) {
					file->balance = 0;
					n->balance = 0;
				} else if (last->balance > 0) {
					file->balance = -1;
					n->balance = 0;
				} else {
					file->balance = 0;
					n->balance = 1;
				}
				last->balance = 0;
				break;
			}
		}
	}
	return true;
}

typedef const unsigned char cuchar;

static struct dirlist *finddir(struct dirlist *dir, cuchar *name, size_t namelen) {
	struct dirlist *d, *this, *parent, *h;
	struct dirlist **stack[128];
	int stackpointer = 0;

	stack[stackpointer++] = &dir->subdirs;
	d = dir->subdirs;

	while (d != NULL) {
		int c;

		if (namelen < d->len) {
			c = memcmp(name, d->name, namelen);
			if (c <= 0) {
				stack[stackpointer++] = &d->nextl;
				d = d->nextl;
			} else {
				stack[stackpointer++] = &d->nextr;
				d = d->nextr;
			}
		} else {
			c = memcmp(name, d->name, d->len);
			if (c == 0 && d->len == namelen) {
				return d;
			} else if (c >= 0) {
				stack[stackpointer++] = &d->nextr;
				d = d->nextr;
			} else {
				stack[stackpointer++] = &d->nextl;
				d = d->nextl;
			}
		}
	}
	/* not found, create it and rebalance */
	d = malloc(sizeof(struct dirlist) + namelen);
	if (FAILEDTOALLOC(d))
		return d;
	d->subdirs = NULL;
	d->nextl = NULL;
	d->nextr = NULL;
	d->balance = 0;
	d->parent = dir;
	d->files = NULL;
	d->len = namelen;
	memcpy(d->name, name, namelen);
	*(stack[--stackpointer]) = d;
	this = d;
	while (stackpointer > 0) {
		parent = *(stack[--stackpointer]);
		if (parent->nextl == this) {
			parent->balance--;
			if (parent->balance > -1)
				break;
			if (parent->balance == -1) {
				this = parent;
				continue;
			}
			if (this->balance == -1) {
				parent->nextl = this->nextr;
				parent->balance = 0;
				this->nextr = parent;
				this->balance = 0;
				*(stack[stackpointer]) = this;
				break;
			} else {
				h = this->nextr;
				parent->nextl = h->nextr;
				*(stack[stackpointer]) = h;
				h->nextr = parent;
				this->nextr = h->nextl;
				h->nextl = this;
				if (h->balance == 0) {
					parent->balance = 0;
					this->balance = 0;
				} else if (h->balance < 0) {
					parent->balance = 1;
					this->balance = 0;
				} else {
					parent->balance = 0;
					this->balance = -1;
				}
				h->balance = 0;
				break;
			}
		} else {
			parent->balance++;
			if (parent->balance < 1)
				break;
			if (parent->balance == 1) {
				this = parent;
				continue;
			}
			if (this->balance == 1) {
				parent->nextr = this->nextl;
				parent->balance = 0;
				this->nextl = parent;
				this->balance = 0;
				*(stack[stackpointer]) = this;
				break;
			} else {
				h = this->nextl;
				parent->nextr = h->nextl;
				*(stack[stackpointer]) = h;
				h->nextl = parent;
				this->nextl = h->nextr;
				h->nextr = this;
				if (h->balance == 0) {
					parent->balance = 0;
					this->balance = 0;
				} else if (h->balance > 0) {
					parent->balance = -1;
					this->balance = 0;
				} else {
					parent->balance = 0;
					this->balance = 1;
				}
				h->balance = 0;
				break;
			}
		}
	}
	return d;
}

static retvalue filelist_addfiles(struct filelist_list *list, const struct filelist_package *package, const char *filekey, const char *datastart, size_t size) {
	struct dirlist *curdir = list->root;
	const unsigned char *data = (const unsigned char *)datastart;

	while (*data != '\0') {
		int d;

		if ((size_t)(data - (const unsigned char *)datastart) >= size-1) {
			/* This might not catch everything, but we are only
			 * accessing it readonly */
			fprintf(stderr, "Corrupted file list data for %s\n",
					filekey);
			return RET_ERROR;
		}
		d = *(data++);
		if (d == 1) {
			size_t len = 0;
			while (*data == 255) {
				data++;
				len += 255;
			}
			if (*data == 0) {
				fprintf(stderr,
					"Corrupted file list data for %s\n",
					filekey);
				return RET_ERROR;
			}
			len += *(data++);
			if (!findfile(curdir, package->name, (const char*)data, len))
				return RET_ERROR_OOM;
			 data += len;
		} else if (d == 2) {
			size_t len = 0;
			while (*data == 255) {
				data++;
				len += 255;
			}
			if (*data == 0) {
				fprintf(stderr,
					"Corrupted file list data for %s\n",
					filekey);
				return RET_ERROR;
			}
			len += *(data++);
			curdir = finddir(curdir, data, len);
			if (FAILEDTOALLOC(curdir))
				return RET_ERROR_OOM;
			data += len;
		} else {
			d -= 2;
			while (d-- > 0 && curdir->parent != NULL)
				curdir = curdir->parent;
		}
	}
	if ((size_t)(data - (const unsigned char *)datastart) != size-1) {
		fprintf(stderr,
"Corrupted file list data for %s (format suggest %llu, is %llu)\n",
				filekey,
				(unsigned long long)(data -
					(const unsigned char *)datastart),
				(unsigned long long)(size-1));
		return RET_ERROR;
	}
	return RET_OK;
}

retvalue filelist_addpackage(struct filelist_list *list, struct package *pkg) {
	const struct filelist_package *package;
	char *debfilename, *contents = NULL;
	retvalue r;
	const char *c;
	size_t len;
	char *section, *filekey;

	r = chunk_getvalue(pkg->control, "Section", &section);
	/* Ignoring packages without section, as they should not exist anyway */
	if (!RET_IS_OK(r))
		return r;
	r = chunk_getvalue(pkg->control, "Filename", &filekey);
	/* dito with filekey */
	if (!RET_IS_OK(r)) {
		free(section);
		return r;
	}

	r = filelist_newpackage(list, pkg->name, section, &package);
	assert (r != RET_NOTHING);
	if (RET_WAS_ERROR(r)) {
		free(filekey);
		free(section);
		return r;
	}

	r = table_gettemprecord(rdb_contents, filekey, &c, &len);
	if (r == RET_NOTHING) {
		if (verbose > 3)
			printf("Reading filelist for %s\n", filekey);
		debfilename = files_calcfullfilename(filekey);
		if (FAILEDTOALLOC(debfilename)) {
			free(filekey);
			free(section);
			return RET_ERROR_OOM;
		}
		r = getfilelist(&contents, &len, debfilename);
		len--;
		free(debfilename);
		c = contents;
	}
	if (RET_IS_OK(r)) {
		r = filelist_addfiles(list, package, filekey, c, len + 1);
		if (contents != NULL)
			r = table_adduniqsizedrecord(rdb_contents, filekey,
					contents, len + 1, true, false);
	}
	free(contents);
	free(filekey);
	free(section);
	return r;
}

retvalue fakefilelist(const char *filekey) {
	return table_adduniqsizedrecord(rdb_contents, filekey,
			"", 1, true, false);
}

static const char separator_chars[] = "\t    ";

static void filelist_writefiles(char *dir, size_t len,
		struct filelist *files, struct filetorelease *file) {
	unsigned int i;
	bool first;

	if (files == NULL)
		return;
	filelist_writefiles(dir, len, files->nextl, file);
	(void)release_writedata(file, dir, len);
	(void)release_writestring(file, files->name);
	(void)release_writedata(file, separator_chars,
			sizeof(separator_chars) - 1);
	first = true;
	for (i = 0 ; i < files->count ; i ++) {
		if (!first)
			(void)release_writestring(file, ",");
		first = false;
		(void)release_writestring(file, files->packages[i]);
	}
	(void)release_writestring(file, "\n");
	filelist_writefiles(dir, len, files->nextr, file);
}

static retvalue filelist_writedirs(char **buffer_p, size_t *size_p, size_t ofs, struct dirlist *dir, struct filetorelease *file) {

	if (dir->nextl != NULL) {
		retvalue r;
		r = filelist_writedirs(buffer_p, size_p, ofs, dir->nextl, file);
		if (RET_WAS_ERROR(r))
			return r;
	}
	{	size_t len = dir->len;
		register retvalue r;

		if (ofs+len+2 >= *size_p) {
			char *n;

			*size_p += 1024*(1+(len/1024));
			n = realloc(*buffer_p, *size_p);
			if (FAILEDTOALLOC(n)) {
				free(*buffer_p);
				*buffer_p = NULL;
				return RET_ERROR_OOM;
			}
			*buffer_p = n;
		}
		memcpy((*buffer_p) + ofs, dir->name, len);
		(*buffer_p)[ofs + len] = '/';
		// TODO: output files and directories sorted together instead
		filelist_writefiles(*buffer_p, ofs+len+1, dir->files, file);
		if (dir->subdirs == NULL)
			r = RET_OK;
		else
			r = filelist_writedirs(buffer_p, size_p, ofs+len+1,
					dir->subdirs, file);
		if (dir->nextr == NULL)
			return r;
		if (RET_WAS_ERROR(r))
			return r;
	}
	return filelist_writedirs(buffer_p, size_p, ofs, dir->nextr, file);
}

retvalue filelist_write(struct filelist_list *list, struct filetorelease *file) {
	size_t size = 1024;
	char *buffer = malloc(size);
	retvalue r;

	if (FAILEDTOALLOC(buffer))
		return RET_ERROR_OOM;

	buffer[0] = '\0';
	filelist_writefiles(buffer, 0, list->root->files, file);
	if (list->root->subdirs != NULL)
		r = filelist_writedirs(&buffer, &size, 0,
				list->root->subdirs, file);
	else
		r = RET_OK;
	free(buffer);
	return r;
}

/* helpers for filelist generators to get the preprocessed form */

retvalue filelistcompressor_setup(/*@out@*/struct filelistcompressor *c) {
	c->size = 2000; c->len = 0;
	c->filelist = malloc(c->size);
	if (FAILEDTOALLOC(c->filelist))
		return RET_ERROR_OOM;
	c->dirdepth = 0;
	return RET_OK;
}

static inline bool filelistcompressor_space(struct filelistcompressor *c, size_t len) {
	if (c->len + len + 2 >= c->size) {
		char *n;

		if (c->size > 1024*1024*1024) {
			fprintf(stderr, "Ridiculously long file list!\n");
			return false;
		}
		c->size = c->len + len + 2048;
		n = realloc(c->filelist, c->size);
		if (FAILEDTOALLOC(n))
			return false;
		c->filelist = n;
	}
	return true;
}

retvalue filelistcompressor_add(struct filelistcompressor *c, const char *name, size_t name_len) {
	unsigned int depth;
	const char *separator;

	/* check if it is already in the current dir or a subdir of that: */
	if (name_len > 0 && *name == '.') {
		name++; name_len--;
	}
	while (name_len > 0 && *name == '/') {
		name++; name_len--;
	}
	for (depth = 0; depth < c->dirdepth ; depth++) {
		const unsigned char *u =(unsigned char *)c->filelist
						+ c->offsets[depth];
		size_t dir_len = 0;
		while (*u == 255) {
			dir_len += 255;
			u++;
		}
		dir_len += *(u++);
		if (dir_len >= name_len)
			break;
		if (memcmp(u, name, dir_len) != 0 || name[dir_len] != '/')
			break;
		name += dir_len + 1;
		name_len -= dir_len + 1;
	}
	if (depth < c->dirdepth) {
		if (!filelistcompressor_space(c, 1))
			return RET_ERROR_OOM;
		c->filelist[c->len++] = (unsigned char)2 +
						c->dirdepth - depth;
		c->dirdepth = depth;
	}
	while ((separator = memchr(name, '/', name_len)) != NULL) {
		size_t dirlen = separator - name;
		/* ignore files within directories with more than 255 chars */
		if (dirlen >= 255)
			return RET_NOTHING;
		/* ignore too deep paths */
		if (c->dirdepth > 252)
			return RET_NOTHING;
		/* add directory */
		if (!filelistcompressor_space(c, 2 + dirlen))
			return RET_ERROR_OOM;
		c->filelist[c->len++] = 2;
		c->offsets[c->dirdepth++] = c->len;
		c->filelist[c->len++] = dirlen;
		memcpy(c->filelist + c->len, name, dirlen);
		c->len += dirlen;
		name += dirlen+1;
		name_len -= dirlen+1;
		while (name_len > 0 && *name == '/') {
			name++; name_len--;
		}
	}
	if (name_len >= 255)
		return RET_NOTHING;
	/* all directories created, now only the file is left */
	if (!filelistcompressor_space(c, 2 + name_len))
		return RET_ERROR_OOM;
	c->filelist[c->len++] = 1;
	c->filelist[c->len++] = name_len;
	memcpy(c->filelist + c->len, name, name_len);
	c->len += name_len;
	return RET_OK;
}

retvalue filelistcompressor_finish(struct filelistcompressor *c, /*@out@*/char **list, /*@out@*/size_t *size) {
	char *l;

	l = realloc(c->filelist, c->len+1);
	if (FAILEDTOALLOC(l)) {
		free(c->filelist);
		return RET_ERROR_OOM;
	}
	l[c->len] = '\0';
	*list = l;
	*size = c->len+1;
	return RET_OK;
}

void filelistcompressor_cancel(struct filelistcompressor *c) {
	free(c->filelist);
}

retvalue filelists_translate(struct table *oldtable, struct table *newtable) {
	retvalue r;
	struct cursor *cursor;
	const char *filekey, *olddata;
	size_t olddata_len, newdata_size;
	char *newdata;

	r = table_newglobalcursor(oldtable, &cursor);
	if (!RET_IS_OK(r))
		return r;
	while (cursor_nexttempdata(oldtable, cursor, &filekey,
				&olddata, &olddata_len)) {
		const char *p;
		size_t l;
		struct filelistcompressor c;

		r = filelistcompressor_setup(&c);
		if (RET_WAS_ERROR(r))
			break;
		for (p = olddata ; (l = strlen(p)) != 0 ; p += l + 1) {
			r = filelistcompressor_add(&c, p, l);
			if (RET_WAS_ERROR(r))
				break;
		}
		if (RET_WAS_ERROR(r)) {
			filelistcompressor_cancel(&c);
			break;
		}
		r = filelistcompressor_finish(&c, &newdata, &newdata_size);
		if (!RET_IS_OK(r))
			break;
		r = table_adduniqsizedrecord(newtable, filekey,
				newdata, newdata_size, false, false);
		free(newdata);
		if (RET_WAS_ERROR(r))
			break;

	}
	if (RET_WAS_ERROR(r)) {
		(void)cursor_close(oldtable, cursor);
		return r;
	}
	r = cursor_close(oldtable, cursor);
	if (RET_WAS_ERROR(r))
		return r;
	return RET_OK;
}
