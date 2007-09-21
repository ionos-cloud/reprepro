/*  This file is part of "reprepro"
 *  Copyright (C) 2006,2007 Bernhard R. Link
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

#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "error.h"
#include "database_p.h"
#include "files.h"
#include "debfile.h"
#include "filelist.h"

extern int verbose;

struct filelist_package {
	struct filelist_package *next;
	char name[];
};

struct dirlist;
struct filelist {
	struct filelist *nextl;
	struct filelist *nextr;
	char *name;
	size_t count;
	const char *packages[];
};
struct dirlist {
	struct dirlist *next, *parent, *subdirs;
	/*@dependant@*/struct dirlist *lastsubdir;
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

	filelist = calloc(1,sizeof(struct filelist_list));
	if( filelist == NULL )
		return RET_ERROR_OOM;
	filelist->root = calloc(1,sizeof(struct dirlist));
	if( filelist->root == NULL ) {
		free(filelist);
		return RET_ERROR_OOM;
	}
	*list = filelist;
	return RET_OK;
};
static void files_free(/*@only@*/struct filelist *list) {
	if( list == NULL )
		return;
	files_free(list->nextl);
	files_free(list->nextr);
	free(list->name);
	free(list);
}
static void dirlist_free(/*@only@*/struct dirlist *list) {
	while( list != NULL ) {
		struct dirlist *h = list->next;
		files_free(list->files);
		dirlist_free(list->subdirs);
		free(list);
		list = h;
	}
}
void filelist_free(struct filelist_list *list) {

	if( list == NULL )
		return;
	dirlist_free(list->root);
	while( list->packages != NULL ) {
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
	if( p == NULL )
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
	struct filelist *file, *n, **p;

	p = &parent->files;
	file = *p;

	while( file != NULL ) {
		int c = strncmp(basefilename, file->name, namelen);
		if( c == 0 && file->name[namelen] == '\0' ) {
			n = realloc(file,sizeof(struct filelist)+
					(file->count+1)*sizeof(const char*));
			if( n == NULL )
				return false;
			n->packages[n->count++] = packagename;
			parent->lastfile = n;
			*p = n;
			return true;
		} else if ( c > 0 ) {
			/* Sorted lists give us a right shift,
			 * go a bit more left to reduce that */
			if( file->nextl == NULL &&
					file->nextr != NULL &&
					file->nextr->nextl == NULL ) {
				file->nextr->nextl = file;
				*p = file->nextr;
				file->nextr = NULL;
			} else
				p = &file->nextr;
			file = *p;
		} else  {
			p = &file->nextl;
			file = *p;
		}
	}
	n = malloc(sizeof(struct filelist)+sizeof(const char*));
	if( n == NULL )
		return false;
	n->name = strndup(basefilename, namelen);
	n->nextl = NULL;
	n->nextr = NULL;
	n->count = 1;
	n->packages[0] = packagename;
	if( n->name == NULL ) {
		free(n);
		return false;
	}
	*p = n;
	parent->lastfile = n;
	return true;
}

typedef const unsigned char cuchar;

static struct dirlist *finddir(struct dirlist *dir, cuchar *name, size_t namelen) {
	struct dirlist **dir_p, *d;

	/* as we go through the list of packages alphabetically, there are
	 * good chances the directory we look for is after the one last looked
	 * at, so we just check this. (can easily gain 50% this way) */
	dir_p = &dir->subdirs;
	if( dir->lastsubdir != NULL ) {
		int c;
		d = dir->lastsubdir;
		if( namelen < d->len ) {
			c = memcmp(name, d->name, namelen);
			if( c > 0 )
				dir_p = &(d->next);
		} else {
			c = memcmp(name, d->name, d->len);
			if( c == 0 && d->len == namelen )
				return d;
			else if( c >= 0)
				dir_p = &(d->next);
		}
	}
	while( (d=*dir_p) != NULL ) {
		int c;

		if( namelen < d->len ) {
			c = memcmp(name, d->name, namelen);
			if( c <= 0 )
				break;
			else
				dir_p = &(d->next);
		} else {
			c = memcmp(name, d->name, d->len);
			if( c == 0 && d->len == namelen ) {
				dir->lastsubdir = d;
				return d;
			} else if( c >= 0) {
				dir_p = &(d->next);
			} else
				break;
		}
	}
	/* not found, but belongs after before the found one */
	d = malloc(sizeof(struct dirlist) + namelen );
	if( d == NULL )
		return d;
	d->next = *dir_p;
	d->parent = dir;
	d->subdirs = NULL;
	d->lastsubdir = NULL;
	d->files = NULL;
	d->lastfile = NULL;
	d->len = namelen;
	memcpy(d->name, name, namelen);
	*dir_p = d;
	return d;
}

static retvalue filelist_addfiles(struct filelist_list *list, const struct filelist_package *package, const char *filekey, const char *datastart, size_t len) {
	struct dirlist *curdir = list->root;
	const unsigned char *data = (const unsigned char *)datastart;

	while( *data != '\0' ) {
		if( (size_t)(data - (const unsigned char *)datastart) >= len ) {
			/* This might not catch everything, but we are only
			 * accessing it readonly */
			fprintf(stderr, "Corrupted file list data for %s\n",
					filekey);
			return RET_ERROR;
		}
		int d = *(data++);
		if( d == 1 ) {
			size_t len = 0;
			while( *data == 255 ) {
				data++;
				len += 255;
			}
			if( *data == 0 ) {
				fprintf(stderr,
					"Corrupted file list data for %s\n",
					filekey);
				return RET_ERROR;
			}
			len += *(data++);
			if( !findfile(curdir, package->name, (const char*)data, len) )
				return RET_ERROR_OOM;
			 data += len;
		} else if( d == 2 ) {
			size_t len = 0;
			while( *data == 255 ) {
				data++;
				len += 255;
			}
			if( *data == 0 ) {
				fprintf(stderr,
					"Corrupted file list data for %s\n",
					filekey);
				return RET_ERROR;
			}
			len += *(data++);
			curdir = finddir(curdir, data, len);
			if( curdir == NULL )
				return RET_ERROR_OOM;
			data += len;
		} else {
			d -= 2;
			while( d-- > 0 && curdir->parent != NULL )
				curdir = curdir->parent;
		}
	}
	return RET_OK;
}

retvalue filelist_addpackage(struct filelist_list *list, struct database *database, const char *packagename, const char *section, const char *filekey) {
	const struct filelist_package *package;
	char *debfilename, *contents = NULL;
	retvalue r;
	const char *c;
	size_t size;

	r = filelist_newpackage(list, packagename, section, &package);
	assert( r != RET_NOTHING );
	if( RET_WAS_ERROR(r) )
		return r;

	r = table_gettemprecord(database->contents, filekey, &c, &size);
	if( r == RET_NOTHING ) {
		if( verbose > 3 )
			printf("Reading filelist for %s\n", filekey);
		debfilename = files_calcfullfilename(database, filekey);
		if( debfilename == NULL ) {
			return RET_ERROR_OOM;
		}
		r = getfilelist(&contents, &size, debfilename);
		free(debfilename);
		c = contents;
	}
	if( RET_IS_OK(r) ) {
		r = filelist_addfiles(list, package, filekey, c, size);
		if( contents != NULL )
			r = table_adduniqlenrecord(database->contents, filekey,
					contents, size, true);
	}
	free(contents);
	return r;
}

retvalue fakefilelist(struct database *database, const char *filekey) {
	return table_adduniqlenrecord(database->contents, filekey,
			"", 1, true);
}

static const char header[] = "FILE                                                    LOCATION\n";
static const char separator[] = "\t    ";

static void filelist_writefiles(char *dir, size_t len,
		struct filelist *files, struct filetorelease *file) {
	unsigned int i;
	bool first;

	if( files == NULL )
		return;
	filelist_writefiles(dir,len,files->nextl,file);
		(void)release_writedata(file,dir,len);
		(void)release_writestring(file,files->name);
		(void)release_writedata(file,separator,sizeof(separator)-1);
		first = true;
		for( i = 0 ; i < files->count ; i ++ ) {
			if( !first )
				(void)release_writestring(file,",");
			first = false;
			(void)release_writestring(file,files->packages[i]);
		}
		(void)release_writestring(file,"\n");
	filelist_writefiles(dir,len,files->nextr,file);
}

static retvalue filelist_writedirs(char **buffer_p, size_t *size_p, char *start,
		struct dirlist *dirs, struct filetorelease *file) {
	struct dirlist *dir;
	size_t len;
	retvalue r;

	for( dir = dirs ; dir != NULL ; dir = dir->next ) {
		len = dir->len;
		if( start+len+2 >= *buffer_p+*size_p ) {
			*size_p += 1024*(1+(len/1024));
			char *n = realloc( *buffer_p, *size_p );
			if( n == NULL ) {
				free(*buffer_p);
			}
			*buffer_p = n;
		}
		memcpy(start, dir->name, len);
		start[len] = '/';
		// TODO: output files and directories sorted together instead
		filelist_writefiles(*buffer_p,1+len+start-*buffer_p,dir->files,file);
		r = filelist_writedirs(buffer_p,size_p,start+len+1,dir->subdirs,file);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

retvalue filelist_write(struct filelist_list *list, struct filetorelease *file) {
	size_t size = 1024;
	char *buffer = malloc(size);
	retvalue r;

	if( buffer == NULL )
		return RET_ERROR_OOM;

	(void)release_writedata(file,header,sizeof(header)-1);
	buffer[0] = '\0';
	filelist_writefiles(buffer,0,list->root->files,file);
	r = filelist_writedirs(&buffer,&size,buffer,list->root->subdirs,file);
	free(buffer);
	return r;
}

/* helpers for filelist generators to get the preprocessed form */

retvalue filelistcompressor_setup(/*@out@*/struct filelistcompressor *c) {
	c->size = 2000; c->len = 0;
	c->filelist = malloc(c->size);
	if( c->filelist == NULL )
		return RET_ERROR_OOM;
	c->dirdepth = 0;
	return RET_OK;
}

static inline bool filelistcompressor_space(struct filelistcompressor *c, size_t len) {
	if( c->len + len + 2 >= c->size ) {
		char *n;

		if( c->size > 1024*1024*1024 ) {
			fprintf(stderr, "Ridicilous long filelist!\n");
			return false;
		}
		c->size = c->len + len + 2048;
		n = realloc(c->filelist, c->size);
		if( n == NULL )
			return false;
		c->filelist = n;
	}
	return true;
}

retvalue filelistcompressor_add(struct filelistcompressor *c, const char *name, size_t name_len) {
	unsigned int depth;
	const char *separator;

	/* check if it is already in the current dir or a subdir of that: */
	if( name_len > 0 && *name == '.' ) {
		name++; name_len--;
	}
	while( name_len > 0 && *name == '/' ) {
		name++; name_len--;
	}
	for( depth = 0; depth < c->dirdepth ; depth++ ) {
		const unsigned char *u =(unsigned char *)c->filelist
						+ c->offsets[depth];
		size_t dir_len = 0;
		while( *u == 255 ) {
			dir_len += 255;
			u++;
		}
		dir_len += *(u++);
		if( dir_len >= name_len )
			break;
		if( memcmp(u, name, dir_len) != 0 || name[dir_len] != '/' )
			break;
		name += dir_len + 1;
		name_len -= dir_len + 1;
	}
	if( depth < c->dirdepth ) {
		if( !filelistcompressor_space(c, 1) )
			return RET_ERROR_OOM;
		c->filelist[c->len++] = (unsigned char)2 +
						c->dirdepth - depth;
		c->dirdepth = depth;
	}
	while( (separator = memchr(name, '/', name_len)) != NULL ) {
		size_t dirlen = separator - name;
		/* ignore files within directories with more than 255 chars */
		if( dirlen >= 255 )
			return RET_NOTHING;
		/* ignore too deep paths */
		if( c->dirdepth > 252 )
			return RET_NOTHING;
		/* add directory */
		if( !filelistcompressor_space(c, 2 + dirlen) )
			return RET_ERROR_OOM;
		c->filelist[c->len++] = 2;
		c->offsets[c->dirdepth++] = c->len;
		c->filelist[c->len++] = dirlen;
		memcpy(c->filelist + c->len, name, dirlen);
		c->len += dirlen;
		name += dirlen+1;
		name_len -= dirlen+1;
		while( name_len > 0 && *name == '/' ) {
			name++; name_len--;
		}
	}
	if( name_len >= 255 )
		return RET_NOTHING;
	/* all directories created, now only the file is left */
	if( !filelistcompressor_space(c, 2 + name_len) )
		return RET_ERROR_OOM;
	c->filelist[c->len++] = 1;
	c->filelist[c->len++] = name_len;
	memcpy(c->filelist + c->len, name, name_len);
	c->len += name_len;
	return RET_OK;
}

retvalue filelistcompressor_finish(struct filelistcompressor *c, /*@out@*/char **list,/*@out@*/size_t *size) {
	char *l;

	l = realloc(c->filelist, c->len+1);
	if( l == NULL ) {
		free(c->filelist);
		return RET_ERROR_OOM;
	}
	c->filelist[c->len] = '\0';
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

	r = table_newglobaluniqcursor(oldtable, &cursor);
	if( !RET_IS_OK(r) )
		return r;
	while( cursor_nexttempdata(oldtable, cursor, &filekey,
				&olddata, &olddata_len) ) {
		const char *p;
		size_t l;
		struct filelistcompressor c;

		r = filelistcompressor_setup(&c);
		if( RET_WAS_ERROR(r) )
			break;
		for( p = olddata ; (l = strlen(p)) != 0 ; p += l + 1 ) {
			r = filelistcompressor_add(&c, p, l);
			if( RET_WAS_ERROR(r) )
				break;
		}
		if( RET_WAS_ERROR(r) ) {
			filelistcompressor_cancel(&c);
			break;
		}
		r = filelistcompressor_finish(&c, &newdata, &newdata_size);
		if( !RET_IS_OK(r) )
			break;
		r = table_adduniqlenrecord(newtable, filekey,
				newdata, newdata_size, false);
		free(newdata);
		if( RET_WAS_ERROR(r) )
			break;

	}
	if( RET_WAS_ERROR(r) ) {
		(void)cursor_close(oldtable, cursor);
		return r;
	}
	r = cursor_close(oldtable, cursor);
	if( RET_WAS_ERROR(r) )
		return r;
	return RET_OK;
}
