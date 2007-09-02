#include <config.h>

#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "error.h"
#include "filelist.h"

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
	struct dirlist *next;
	char *name;
	struct dirlist *subdirs;
	/*@dependant@*/struct dirlist *lastsubdir;
	struct filelist *files;
	/*@dependant@*/struct filelist *lastfile;
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
/*
		struct filelist *f = list->files;

		while( f != NULL ) {
			struct filelist *fh = f;
			f = f->next;
			free(fh->name);
			free(fh);
		}
*/
		free(list->name);
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

retvalue filelist_newpackage(struct filelist_list *filelist, const char *name, const char *section, const struct filelist_package **pkg) {
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

static int
findfile(const char *basefilename, struct dirlist *parent,
		const struct filelist_package *package) {
	struct filelist *file,*n,**p;

/*	if( parent->lastfile != NULL &&
			strcmp(basefilename,parent->lastfile->name) > 0 )
		p = &parent->lastfile->next;
	else */
		p = &parent->files;
	file = *p;

	while( file != NULL ) {
		int c = strcmp(basefilename, file->name);
		if( c == 0 ) {
			n = realloc(file,sizeof(struct filelist)+
					(file->count+1)*sizeof(const char*));
			if( n == NULL ) {
				return -1;
			}
			n->packages[n->count++] = package->name;
			parent->lastfile = n;
			*p = n;
			return 1;
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
	if( n == NULL ) {
		return -1;
	}
	n->name = strdup(basefilename);
	n->nextl = NULL;
	n->nextr = NULL;
	n->count = 1;
	n->packages[0] = package->name;
	if( n->name == NULL ) {
		free(n);
		return -1;
	}
	*p = n;
	parent->lastfile = n;
	return 1;
}

static int search(const char *p, struct dirlist *dir, const struct filelist_package *package) {
	const char *q;
	struct dirlist **dir_p,*d;
	size_t len;

	q = p;
	while( *q != '\0' && *q != '/' )
		q++;
	if( *q == '\0' ) {
		return findfile(p,dir,package);
	}
	if( dir->lastsubdir != NULL ) {
		len = *(unsigned char*)dir->lastsubdir->name;
		if( len == (size_t)(q-p) &&
				strncmp(p,dir->lastsubdir->name+1,len)==0 ) {
			assert( p[len] == '/' );
			return search(q+1, dir->lastsubdir, package);
		}
	}
	dir_p = &dir->subdirs;
	while( (d=*dir_p) != NULL ) {
		int c;

		len = *(unsigned char*)d->name;
		c = strncmp(p,d->name+1,len);
		if( c == 0 && p[len] == '/' ) {
			assert( (size_t)(q-p) == len );
			dir->lastsubdir = d;
			return search(q+1,d,package);
		} else if( c >= 0 ) {
			dir_p = &(d->next);
		} else
			break;
	}
	d = malloc(sizeof(struct dirlist));
	if( d == NULL ) {
		return -1;
	}
	d->next = *dir_p;
	d->name = malloc((q-p)+1);
	if( d->name == NULL ) {
		free(d);
		return -1;
	}
	*dir_p = d;
	len = q-p;
	*d->name = len;
	memcpy(d->name+1,p,len);
	d->subdirs = NULL;
	d->files = NULL;
	d->lastsubdir = NULL;
	d->lastfile = NULL;
	dir->lastsubdir = d;
	return search(q+1,d,package);
}

retvalue filelist_add(struct filelist_list *list,const struct filelist_package *package,const char *filekey) {
 	int r;

	r = search(filekey, list->root, package);
	assert( r != 0 );
	return (r < 0)?RET_ERROR_OOM:RET_OK;
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
		len = *dir->name;
		if( start+len+2 >= *buffer_p+*size_p ) {
			*size_p += 1024;
			char *n = realloc( *buffer_p, *size_p );
			if( n == NULL ) {
				free(*buffer_p);
			}
			*buffer_p = n;
		}
		memcpy(start,dir->name+1,len);
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
