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

#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#define DEFINE_IGNORE_VARIABLES
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "copyfile.h"
#include "md5sum.h"
#include "chunks.h"
#include "chunkedit.h"
#include "signature.h"
#include "debfile.h"

/* for compatibility with used code */
int verbose=0;
bool_t interrupted(void) {return FALSE;}

static void about(bool_t help) {
	fprintf(help?stdout:stderr,
"modifychanges: Modify a Debian style .changes file\n"
"Syntax: modifychanges <changesfile> <commands>\n"
"Possible commands include:\n"
" verify\n"
" updatechecksums [<files to update>]\n"
" includeallsources [<files to copy from .dsc to .changes>]\n"
//" add <filenames>\n"
//" create <.dsc and .deb files to include>\n"
);
	if( help )
		exit(EXIT_SUCCESS);
	else
		exit(EXIT_FAILURE);
}

struct binaryfile {
	struct binaryfile *next; // in binaries.files list
	struct binary *binary; // parent
	struct fileentry *file;
	char *controlchunk;
	char *name, *version, *architecture;
	char *sourcename, *sourceversion;
	char *maintainer;
	char *section, *priority;
	char *shortdescription;
	bool_t hasmd5sums;
};

static void binaryfile_free(struct binaryfile *p) {
	if( p == NULL )
		return;

	free(p->controlchunk);
	free(p->name);
	free(p->version);
	free(p->architecture);
	free(p->sourcename);
	free(p->sourceversion);
	free(p->maintainer);
	free(p->section);
	free(p->priority);
	free(p->shortdescription);
	free(p);
}

enum filetype { ft_UNKNOWN,
			ft_TAR_GZ, ft_ORIG_TAR_GZ, ft_DIFF_GZ,
	                ft_TAR_BZ2, ft_ORIG_TAR_BZ2, ft_DIFF_BZ2,
#define ft_MaxInSource ft_DSC-1
			ft_DSC, ft_DEB, ft_UDEB , ft_Count};
#define ft_Max ft_Count-1
static const char * const typesuffix[ft_Count] = { "?",
	".tar.gz", ".orig.tar.gz", ".diff.gz",
	".tar.bz2", ".orig.tar.bz2", ".diff.bz2",
	".dsc", ".deb", ".udeb"};

struct dscfile {
	struct fileentry *file;
	char *name;
	char *version;
	struct strlist binaries;
	char *maintainer;
	char *controlchunk;
	struct strlist validkeys,keys;
	// hard to get:
	char *section, *priority;
	// TODO: check Architectures?
	size_t filecount;
	struct sourcefile {
		char *basename;
		char *expectedmd5sum;
		struct fileentry *file;
	} *files;
	bool_t parsed, modified;
};

static void dscfile_free(struct dscfile *p) {
	unsigned int i;

	if( p == NULL )
		return;

	free(p->name);
	free(p->version);
	free(p->maintainer);
	free(p->controlchunk);
	strlist_done(&p->keys);
	strlist_done(&p->validkeys);
	free(p->section);
	free(p->priority);
	if( p->files != NULL )
		for( i = 0 ; i < p->filecount ; i++ ) {
			free(p->files[i].basename);
			free(p->files[i].expectedmd5sum);
		}
	free(p->files);
	free(p);
}

struct fileentry {
	struct fileentry *next;
	char *basename; size_t namelen;
	char *fullfilename;
	char *changesmd5sum, /* NULL means was not listed there yet */
	     *realmd5sum;
	char *section, *priority;
	enum filetype type;
	/* only if type deb or udeb */
	struct binaryfile *deb;
	/* only if type dsc */
	struct dscfile *dsc;
	int refcount;
};
struct changes;
static struct fileentry *add_fileentry(struct changes *c, const char *basename, size_t len, bool_t source);

struct changes {
	char *name;
	char *version;
	char *maintainer;
	char *control;
	struct strlist architectures;
	struct strlist distributions;
	size_t binarycount;
	struct binary {
		char *name;
		char *description;
		struct binaryfile *files;
		bool_t missedinheader, uncheckable;
	} *binaries;
	struct fileentry *files;
	bool_t modified;
};

static void changes_free(struct changes *c) {
	unsigned int i;

	if( c == NULL )
		return;

	free(c->name);
	free(c->version);
	free(c->maintainer);
	free(c->control);
	strlist_done(&c->architectures);
	strlist_done(&c->distributions);
	for( i = 0 ; i < c->binarycount ; i++ ) {
		free(c->binaries[i].name);
		free(c->binaries[i].description);
		// .files belongs elsewhere
	}
	free(c->binaries);
	while( c->files ) {
		struct fileentry *f = c->files;
		c->files = f->next;
		free(f->basename);
		free(f->fullfilename);
		free(f->changesmd5sum);
		free(f->realmd5sum);
		free(f->section);
		free(f->priority);
		if( f->type == ft_DEB || f->type == ft_UDEB ) {
			binaryfile_free(f->deb);
		} else if( f->type == ft_DSC ) {
			dscfile_free(f->dsc);
		}
		free(f);
	}
	free(c);
}

static struct fileentry *add_fileentry(struct changes *c, const char *basename, size_t len, bool_t source) {
	struct fileentry **fp = &c->files;
	struct fileentry *f;
	while( (f=*fp) != NULL ) {
		if( f->namelen == len &&
				strncmp(basename,f->basename,len) == 0 )
			break;
		fp = &f->next;
	}
	if( f == NULL ) {
		f = calloc(1,sizeof(struct fileentry));
		if( f == NULL )
			return NULL;
		*fp = f;
		f->basename = strndup(basename,len);
		f->namelen = len;
		/* guess type */
		for( f->type = source?ft_MaxInSource:ft_Max ;
				f->type > ft_UNKNOWN ; f->type-- ) {
			size_t l = strlen(typesuffix[f->type]);
			if( len > l && strcmp(f->basename+(len-l),
						typesuffix[f->type]) == 0 )
				break;
		}
	}
	return f;
}


static struct binary *get_binary(struct changes *c, const char *p, size_t len) {
	unsigned int j;

	for( j = 0 ; j < c->binarycount ; j++ ) {
		if( strncmp(c->binaries[j].name, p, len) == 0 &&
				c->binaries[j].name[len] == '\0' )
			break;
	}
	if( j == c->binarycount ) {
		char *name = strndup(p, len);
		struct binary *n;

		if( name == NULL )
			return NULL;
		n = realloc(c->binaries,(j+1)*sizeof(struct binary));
		if( n == NULL ) {
			free(name);
			return NULL;
		}
		c->binaries = n;
		c->binarycount = j+1;
		c->binaries[j].name = name;
		c->binaries[j].description = NULL;
		c->binaries[j].files = NULL;
		c->binaries[j].missedinheader = TRUE;
		c->binaries[j].uncheckable = FALSE;
	}
	assert( j < c->binarycount );
	return &c->binaries[j];
}

static retvalue parse_changes_description(struct changes *c, struct strlist *tmp) {
	int i;

	for( i = 0 ; i < tmp->count ; i++ ) {
		struct binary *b;
		const char *p = tmp->values[i];
		const char *e = p;
		const char *d;
		while( *e != '\0' && *e != ' ' && *e != '\t' )
			e++;
		d = e;
		while( *d == ' ' || *d == '\t' )
			d++;
		if( *d == '-' )
			d++;
		while( *d == ' ' || *d == '\t' )
			d++;

		b = get_binary(c, p, e-p);
		if( b == NULL )
			return RET_ERROR_OOM;

		b->description = strdup(d);
		if( b->description == NULL )
			return RET_ERROR_OOM;
	}
	return RET_OK;
}

static retvalue parse_changes_files(struct changes *c, struct strlist *tmp) {
	int i;
	struct fileentry *f;

	for( i = 0 ; i < tmp->count ; i++ ) {
		const char *p,*md5start, *md5end, *sizestart, *sizeend, *sectionstart, *sectionend, *priostart, *prioend, *filestart, *fileend;
		p = tmp->values[i];
#undef xisspace
#define xisspace(c) (c == ' ' || c == '\t')
		while( *p !='\0' && xisspace(*p) )
			p++;
		md5start = p;
		while( *p !='\0' && !xisspace(*p) )
			p++;
		md5end = p;
		while( *p !='\0' && xisspace(*p) )
			p++;
		sizestart = p;
		while( *p !='\0' && !xisspace(*p) )
			p++;
		sizeend = p;
		while( *p !='\0' && xisspace(*p) )
			p++;
		sectionstart = p;
		while( *p !='\0' && !xisspace(*p) )
			p++;
		sectionend = p;
		while( *p !='\0' && xisspace(*p) )
			p++;
		priostart = p;
		while( *p !='\0' && !xisspace(*p) )
			p++;
		prioend = p;
		while( *p !='\0' && xisspace(*p) )
			p++;
		filestart = p;
		while( *p !='\0' && !xisspace(*p) )
			p++;
		fileend = p;
		while( *p !='\0' && xisspace(*p) )
			p++;
		if( *p != '\0' ) {
			fprintf(stderr,"Unexpected sixth argument in '%s'!\n", tmp->values[i]);
			return RET_ERROR;
		}
		f = add_fileentry(c, filestart, fileend-filestart, FALSE);
		if( f->changesmd5sum != NULL ) {
			fprintf(stderr, "WARNING: Multiple occourance of '%s' in .changes file!\nIgnoring all but the first one.\n",
					f->basename);
			continue;
		}
		f->changesmd5sum = names_concatmd5sumandsize(md5start,md5end,sizestart,sizeend);
		if( f->changesmd5sum == NULL )
			return RET_ERROR_OOM;
		if( sectionend - sectionstart == 1 && *sectionstart == '-' ) {
			f->section = NULL;
		} else {
			f->section = strndup(sectionstart,sectionend-sectionstart);
			if( f->section == NULL )
				return RET_ERROR_OOM;
		}
		if( prioend - priostart == 1 && *priostart == '-' ) {
			f->priority = NULL;
		} else {
			f->priority = strndup(priostart,prioend-priostart);
			if( f->priority == NULL )
				return RET_ERROR_OOM;
		}
	}

	return RET_OK;
}

static retvalue parse_dsc(struct fileentry *dscfile, struct changes *changes) {
	struct dscfile *n;
	struct strlist tmp;
	retvalue r;

	if( dscfile->fullfilename == NULL )
		return RET_NOTHING;
	n = calloc(1,sizeof(struct dscfile));
	if( n == NULL )
		return RET_ERROR_OOM;
	r = signature_readsignedchunk(dscfile->fullfilename,
			&n->controlchunk, &n->validkeys, &n->keys, NULL);
	assert( r != RET_NOTHING );
	// TODO: can this be ignored sometimes?
	if( RET_WAS_ERROR(r) ) {
		free(n);
		return r;
	}
	r = chunk_getname(n->controlchunk, "Source",&n->name,FALSE);
	if( RET_WAS_ERROR(r) ) {
		dscfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Maintainer",&n->maintainer);
	if( RET_WAS_ERROR(r) ) {
		dscfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Version",&n->version);
	if( RET_WAS_ERROR(r) ) {
		dscfile_free(n);
		return r;
	}

	/* unusally not here, but hidden in the contents */
	r = chunk_getvalue(n->controlchunk, "Section",&n->section);
	if( RET_WAS_ERROR(r) ) {
		dscfile_free(n);
		return r;
	}
	/* dito */
	r = chunk_getvalue(n->controlchunk, "Priority",&n->priority);
	if( RET_WAS_ERROR(r) ) {
		dscfile_free(n);
		return r;
	}
	r = chunk_getextralinelist(n->controlchunk, "Files", &tmp);
	if( RET_IS_OK(r) && tmp.count > 0 ) {
		int i,j = 0;
		n->files = calloc(tmp.count, sizeof(struct sourcefile));
		for( i = 0 ; i < tmp.count ; i++ ) {
			r = calc_parsefileline(tmp.values[i],
					&n->files[j].basename,
					&n->files[j].expectedmd5sum);
			if( RET_WAS_ERROR(r) ) {
				strlist_done(&tmp);
				dscfile_free(n);
				return r;
			}
			if( RET_IS_OK(r) ) {
				n->files[j].file = add_fileentry(changes,
						n->files[j].basename,
						strlen(n->files[j].basename),
						TRUE);
				j++;
				n->filecount = j;
				if( n->files[j-1].file == NULL ) {
					strlist_done(&tmp);
					dscfile_free(n);
					return RET_ERROR_OOM;
				}
			}
		}
		strlist_done(&tmp);
	} else if( RET_WAS_ERROR(r) ) {
		dscfile_free(n);
		return r;
	}
	dscfile->dsc = n;
	return RET_OK;
}

#define DSC_WRITE_FILES 1
#define DSC_WRITE_ALL 0xFFFF
#define flagset(a) (flags & a) != 0

static retvalue write_dsc_file(struct fileentry *dscfile, unsigned int flags) {
	struct dscfile *dsc = dscfile->dsc;
	unsigned int i;
	struct chunkeditfield *cef;
	retvalue r;
	char *control; size_t controllen;
	char *md5sum;
	char *destfilename;

	if( flagset(DSC_WRITE_FILES) ) {
		cef = cef_newfield("Files", CEF_ADD, CEF_LATE, dsc->filecount, NULL);
		if( cef == NULL )
			return RET_ERROR_OOM;
		for( i = 0 ; i < dsc->filecount ; i++ ) {
			struct sourcefile *f = &dsc->files[i];
			cef_setline(cef, i, 2,
					f->expectedmd5sum, f->basename, NULL);
		}
	} else
		cef = NULL;

	r = chunk_edit(dsc->controlchunk, &control, &controllen, cef);
	cef_free(cef);
	if( RET_WAS_ERROR(r) )
		return r;
	assert( RET_IS_OK(r) );

	// TODO: try to add the signatures to it again...

	// TODO: add options to place changed files in different directory...
	if( dscfile->fullfilename != NULL ) {
		destfilename = strdup(dscfile->fullfilename);
	} else
		destfilename = strdup(dscfile->basename);
	if( destfilename == NULL ) {
		free(control);
		return RET_ERROR_OOM;
	}

	r = md5sum_replace(destfilename, control, controllen, &md5sum);
	if( RET_WAS_ERROR(r) ) {
		free(destfilename);
		free(control);
		return r;
	}
	assert( RET_IS_OK(r) );

	free(dscfile->fullfilename);
	dscfile->fullfilename = destfilename;
	free(dscfile->realmd5sum);
	dscfile->realmd5sum = md5sum;
	free(dsc->controlchunk);
	dsc->controlchunk = control;
	return RET_OK;
}

static retvalue parse_deb(struct fileentry *debfile, struct changes *changes) {
	retvalue r;
	struct binaryfile *n;

	if( debfile->fullfilename == NULL )
		return RET_NOTHING;
	n = calloc(1,sizeof(struct binaryfile));
	if( n == NULL )
		return RET_ERROR_OOM;

	r = extractcontrol(&n->controlchunk, debfile->fullfilename);
	if( !RET_IS_OK(r)) {
		free(n);
		if( r == RET_ERROR_OOM )
			return r;
		else
			return RET_NOTHING;
	}

	r = chunk_getname(n->controlchunk, "Package", &n->name, FALSE);
	if( RET_WAS_ERROR(r) ) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Version", &n->version);
	if( RET_WAS_ERROR(r) ) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getnameandversion(n->controlchunk, "Source",
			&n->sourcename, &n->sourceversion);
	if( RET_WAS_ERROR(r) ) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Maintainer", &n->maintainer);
	if( RET_WAS_ERROR(r) ) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Architecture", &n->architecture);
	if( RET_WAS_ERROR(r) ) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Section",&n->section);
	if( RET_WAS_ERROR(r) ) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Priority",&n->priority);
	if( RET_WAS_ERROR(r) ) {
		binaryfile_free(n);
		return r;
	}
	r = chunk_getvalue(n->controlchunk, "Description",&n->shortdescription);
	if( RET_WAS_ERROR(r) ) {
		binaryfile_free(n);
		return r;
	}
	if( n->name != NULL ) {
		n->binary = get_binary(changes, n->name, strlen(n->name));
		if( n->binary == NULL ) {
			binaryfile_free(n);
			return RET_ERROR_OOM;
		}
		n->next = n->binary->files;
		n->binary->files = n;
	}

	debfile->deb = n;
	return RET_OK;
}

static retvalue processfiles(const char *changesfilename, struct changes *changes,
		const struct strlist *searchpath) {
	char *dir;
	struct fileentry *file;
	retvalue r;

	r = dirs_getdirectory(changesfilename, &dir);
	if( RET_WAS_ERROR(r) )
		return r;

	for( file = changes->files; file != NULL ; file = file->next ) {
		int i; bool_t found;
		char *fullname = calc_dirconcat(dir,file->basename);
		if( fullname == NULL )
			return RET_ERROR_OOM;
		assert( file->fullfilename == NULL );

		found = isregularfile(fullname);
		i = 0;
		while( !found && searchpath != NULL && i < searchpath->count ) {
			free(fullname);
			fullname = calc_dirconcat(searchpath->values[i],
					file->basename);
			if( fullname == NULL )
				return RET_ERROR_OOM;
		}

		r = RET_NOTHING;
		if( found ) {
			file->fullfilename = fullname;
			if( file->type == ft_DSC )
				r = parse_dsc(file,changes);
			else if( file->type == ft_DEB || file->type == ft_UDEB )
				r = parse_deb(file,changes);
			if( RET_WAS_ERROR(r) )
				return r;
		} else
			free(fullname);

		if( r == RET_NOTHING ) {
			/* apply heuristics when not readable */
			if( file->type == ft_DSC ) {
			} else if( file->type == ft_DEB || file->type == ft_UDEB ) {
				struct binary *b; size_t len;

				len = 0;
				while( file->basename[len] != '_' &&
						file->basename[len] != '\0' )
					len++;
				b = get_binary(changes, file->basename, len);
				if( b == NULL )
					return RET_ERROR_OOM;
				b->uncheckable = TRUE;
			}
		}
	}
	free(dir);
	return RET_OK;
}

static retvalue parse_changes(const char *changesfile, const char *chunk, struct changes **changes, const struct strlist *searchpath) {
	retvalue r;
	struct strlist tmp;
#define R if( RET_WAS_ERROR(r) ) { changes_free(n); return r; }

	struct changes *n = calloc(1,sizeof(struct changes));
	if( n == NULL )
		return RET_ERROR_OOM;
	// TODO: do getname here? trim spaces?
	r = chunk_getvalue(chunk, "Source", &n->name);
	R;
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Missing 'Source:' field in %s!\n", changesfile);
		n->name = NULL;
	}
	r = chunk_getvalue(chunk, "Version", &n->version);
	R;
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Missing 'Version:' field in %s!\n", changesfile);
		n->version = NULL;
	}
	r = chunk_getwordlist(chunk, "Architecture", &n->architectures);
	R;
	if( r == RET_NOTHING )
		strlist_init(&n->architectures);
	r = chunk_getwordlist(chunk, "Distribution", &n->distributions);
	R;
	if( r == RET_NOTHING )
		strlist_init(&n->distributions);
	r = chunk_getvalue(chunk, "Maintainer", &n->maintainer);
	R;
	if( r == RET_NOTHING ) {
		fprintf(stderr, "Missing 'Maintainer:' field in %s!\n", changesfile);
		n->maintainer = NULL;
	}
	r = chunk_getwordlist(chunk, "Binary", &tmp);
	R;
	if( r == RET_NOTHING ) {
		n->binaries = NULL;
	} else {
		int i;

		assert( RET_IS_OK(r) );
		n->binaries = calloc(tmp.count, sizeof(struct binary));
		if( n->binaries == NULL ) {
			changes_free(n);
			return RET_ERROR_OOM;
		}
		for( i = 0 ; i < tmp.count ; i++ ) {
			n->binaries[i].name = tmp.values[i];
		}
		n->binarycount = tmp.count;
		free(tmp.values);
	}
	r = chunk_getextralinelist(chunk, "Description", &tmp);
	R;
	if( RET_IS_OK(r) ) {
		r = parse_changes_description(n, &tmp);
		strlist_done(&tmp);
		if( RET_WAS_ERROR(r) ) {
			changes_free(n);
			return RET_ERROR_OOM;
		}
	}

	r = chunk_getextralinelist(chunk, "Files", &tmp);
	R;
	if( RET_IS_OK(r) ) {
		r = parse_changes_files(n, &tmp);
		strlist_done(&tmp);
		if( RET_WAS_ERROR(r) ) {
			changes_free(n);
			return RET_ERROR_OOM;
		}
	}
	r = processfiles(changesfile, n, searchpath);
	R;
	*changes = n;
	return RET_OK;
}

#define CHANGES_WRITE_FILES		0x01
#define CHANGES_WRITE_BINARIES		0x02
#define CHANGES_WRITE_SOURCE		0x04
#define CHANGES_WRITE_VERSION		0x08
#define CHANGES_WRITE_ARCHITECTURES	0x10
#define CHANGES_WRITE_MAINTAINER 	0x20
#define CHANGES_WRITE_ALL 	      0xFFFF

static retvalue write_changes_file(const char *changesfilename,struct changes *c, unsigned int flags) {
	struct chunkeditfield *cef;
	char datebuffer[100];
	retvalue r;
	char *control; size_t controllen;
	unsigned int filecount = 0;
	struct fileentry *f;
	struct tm *tm; time_t t;
	unsigned int i;
	struct strlist binaries;

	strlist_init(&binaries);

	for( f = c->files; f != NULL ; f = f->next ) {
		if( f->changesmd5sum != NULL )
			filecount++;
	}

	if( flagset(CHANGES_WRITE_FILES) ) {
		cef = cef_newfield("Files", CEF_ADD, CEF_LATE, filecount, NULL);
		if( cef == NULL )
			return RET_ERROR_OOM;
		for( i=0,f = c->files; f != NULL ; i++,f = f->next ) {
			if( f->changesmd5sum == NULL )
				continue;
			cef_setline(cef, i, 4,
					f->changesmd5sum,
					f->section?f->section:"-",
					f->priority?f->priority:"-",
					f->basename, NULL);
		}
		assert( i == filecount );
	} else {
		cef = cef_newfield("Files", CEF_KEEP, CEF_LATE, 0, NULL);
		if( cef == NULL )
			return RET_ERROR_OOM;
	}
	cef = cef_newfield("Changes", CEF_KEEP, CEF_LATE, 0, cef);
	if( cef == NULL )
			return RET_ERROR_OOM;
	cef = cef_newfield("Closes", CEF_KEEP, CEF_LATE, 0, cef);
	if( cef == NULL )
			return RET_ERROR_OOM;
	if( flagset(CHANGES_WRITE_BINARIES) ) {
		unsigned int count = 0;
		for( i = 0 ; i < c->binarycount ; i++ ) {
			const struct binary *b = c->binaries + i;
			if( b->description != NULL )
				count++;
		}
		cef = cef_newfield("Description", CEF_ADD, CEF_LATE,
				count, cef);
		if( cef == NULL )
			return RET_ERROR_OOM;
		count = 0;
		for( i = 0 ; i < c->binarycount ; i++ ) {
			const struct binary *b = c->binaries + i;
			if( b->description == NULL )
				continue;
			cef_setline(cef, count++, 3,
					b->name,
					"-",
					b->description,
					NULL);
		}

	}
	// Changed-by: line
	if( flagset(CHANGES_WRITE_MAINTAINER) ) {
		cef = cef_newfield("Maintainer", CEF_ADD, CEF_EARLY, 0, cef);
		if( cef == NULL )
			return RET_ERROR_OOM;
		cef_setdata(cef, c->maintainer);
	} else {
		cef = cef_newfield("Maintainer", CEF_KEEP, CEF_EARLY, 0, cef);
		if( cef == NULL )
			return RET_ERROR_OOM;
	}
	cef = cef_newfield("Urgency", CEF_KEEP, CEF_EARLY, 0, cef);
	if( cef == NULL )
			return RET_ERROR_OOM;
	cef = cef_newfield("Distribution", CEF_KEEP, CEF_EARLY, 0, cef);
	if( cef == NULL )
			return RET_ERROR_OOM;
	if( c->version != NULL ) {
		if( flagset(CHANGES_WRITE_VERSION) )
			cef = cef_newfield("Version", CEF_ADD,
					CEF_EARLY, 0, cef);
		else
			cef = cef_newfield("Version", CEF_ADDMISSED,
					CEF_EARLY, 0, cef);
		if( cef == NULL )
			return RET_ERROR_OOM;
		cef_setdata(cef, c->version);
	} else if( flagset(CHANGES_WRITE_VERSION) ) {
		cef = cef_newfield("Version", CEF_DELETE,
				CEF_EARLY, 0, cef);
		if( cef == NULL )
			return RET_ERROR_OOM;
	}
	if( flagset(CHANGES_WRITE_ARCHITECTURES) ) {
		cef = cef_newfield("Architecture", CEF_ADD, CEF_EARLY, 0, cef);
		if( cef == NULL )
			return RET_ERROR_OOM;
		cef_setwordlist(cef, &c->architectures);
	} else {
		cef = cef_newfield("Architecture", CEF_KEEP, CEF_EARLY, 0, cef);
		if( cef == NULL )
			return RET_ERROR_OOM;
	}
	if( flagset(CHANGES_WRITE_BINARIES) ) {
		r = strlist_init_n(c->binarycount, &binaries);
		if( RET_WAS_ERROR(r) ) {
			cef_free(cef);
			return r;
		}
		assert( RET_IS_OK(r) );
		for( i = 0 ; i < c->binarycount ; i++ ) {
			const struct binary *b = c->binaries + i;
			if( !b->missedinheader ) {
				r = strlist_add_dup(&binaries, b->name);
				if( RET_WAS_ERROR(r) ) {
					strlist_done(&binaries);
					cef_free(cef);
					return r;
				}
			}
		}
		cef = cef_newfield("Binary", CEF_ADD, CEF_EARLY, 0, cef);
		if( cef == NULL ) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
		cef_setwordlist(cef, &binaries);
	} else {
		cef = cef_newfield("Binary", CEF_KEEP, CEF_EARLY, 0, cef);
		if( cef == NULL )
			return RET_ERROR_OOM;
	}
	if( c->name != NULL ) {
		if( flagset(CHANGES_WRITE_SOURCE) )
			cef = cef_newfield("Source", CEF_ADD,
					CEF_EARLY, 0, cef);
		else
			cef = cef_newfield("Source", CEF_ADDMISSED,
					CEF_EARLY, 0, cef);
		if( cef == NULL ) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
		cef_setdata(cef, c->name);
	} else if( flagset(CHANGES_WRITE_SOURCE) ) {
		cef = cef_newfield("Source", CEF_DELETE,
				CEF_EARLY, 0, cef);
		if( cef == NULL ) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
	}
	// TODO: if localized make sure this uses C locale....
	t = time(NULL);
        if( (tm = localtime(&t)) != NULL &&
	    strftime(datebuffer, sizeof(datebuffer)-1,
		    "%a, %e %b %Y %H:%M:%S %Z", tm) > 0 ) {
		cef = cef_newfield("Date", CEF_ADD, CEF_EARLY, 0, cef);
		if( cef == NULL ) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
		cef_setdata(cef, datebuffer);
	} else {
		cef = cef_newfield("Date", CEF_DELETE,
				CEF_EARLY, 0, cef);
		if( cef == NULL ) {
			strlist_done(&binaries);
			return RET_ERROR_OOM;
		}
	}
	cef = cef_newfield("Format", CEF_ADDMISSED, CEF_EARLY, 0, cef);
	if( cef == NULL ) {
		strlist_done(&binaries);
		return RET_ERROR_OOM;
	}
	cef_setdata(cef, "1.7");

	r = chunk_edit(c->control, &control, &controllen, cef);
	strlist_done(&binaries);
	cef_free(cef);
	if( RET_WAS_ERROR(r) )
		return r;
	assert( RET_IS_OK(r) );

	// TODO: try to add the signatures to it again...

	// TODO: add options to place changed files in different directory...

	r = md5sum_replace(changesfilename, control, controllen, NULL);
	if( RET_WAS_ERROR(r) ) {
		free(control);
		return r;
	}
	assert( RET_IS_OK(r) );

	free(c->control);
	c->control = control;
	return RET_OK;
}

static retvalue getmd5sums(struct changes *changes) {
	struct fileentry *file;
	retvalue r;

	for( file = changes->files; file != NULL ; file = file->next ) {

		if( file->fullfilename == NULL )
			continue;
		assert( file->realmd5sum == NULL );

		r = md5sum_read(file->fullfilename, &file->realmd5sum);
		if( r == RET_ERROR_OOM )
			return r;
		else if( !RET_IS_OK(r) ) {
			// assume everything else is not fatal and means
			// a file not readable...
			file->realmd5sum = NULL;
		}
	}
	return RET_OK;
}

static void verify_sourcefile_md5sums(struct sourcefile *f, const char *dscfile) {
	if( f->file == NULL ) {
		if( endswith(f->basename, typesuffix[ft_ORIG_TAR_GZ])
		|| endswith(f->basename, typesuffix[ft_ORIG_TAR_BZ2])) {
			fprintf(stderr,
"Could not check md5sum of '%s', as not included.\n",
				f->basename);
		} else {
			fprintf(stderr,
"ERROR: File '%s' mentioned in '%s' was not found and is not mentioned in the .changes!\n",
				f->basename, dscfile);
		}
		return;
	}
	if( strcmp(f->expectedmd5sum, f->file->realmd5sum) != 0 ) {
		if( strcmp(f->expectedmd5sum, f->file->changesmd5sum) == 0 )
			fprintf(stderr,
"ERROR: '%s' lists the same wrong md5sum for '%s' like the .changes file!\n",
				dscfile, f->basename);
		else
			fprintf(stderr,
"ERROR: '%s' says '%s' has md5sum %s but it has %s!\n",
				dscfile, f->basename,
				f->expectedmd5sum, f->file->realmd5sum);
	}
}

static void verify_binary_name(const char *basename, const char *name, const char *version, const char *architecture, enum filetype type) {
	size_t nlen, vlen, alen;
	if( name == NULL )
		return;
	nlen = strlen(name);
	if( strncmp(basename, name, nlen) != 0 || basename[nlen] != '_' ) {
		fprintf(stderr,
"ERROR: '%s' does not start with '%s_' as expected!\n",
					basename, name);
		return;
	}
	if( version == NULL )
		return;
	vlen = strlen(version);
	if( strncmp(basename+nlen+1, version, vlen) != 0
	|| basename[nlen+1+vlen] != '_' ) {
		fprintf(stderr,
"ERROR: '%s' does not start with '%s_%s_' as expected!\n",
			basename, name, version);
		return;
	}
	if( architecture == NULL )
		return;
	alen = strlen(architecture);
	if( strncmp(basename+nlen+1+vlen+1, architecture, alen) != 0
	|| strcmp(basename+nlen+1+vlen+1+alen, typesuffix[type]) != 0 )
		fprintf(stderr,
"ERROR: '%s' is not called '%s_%s_%s%s' as expected!\n",
			basename, name, version, architecture, typesuffix[type]);
}

static retvalue verify(const char *changesfilename, struct changes *changes) {
	retvalue r;
	struct fileentry *file;
	unsigned int j;

	printf("Checking Source packages...\n");
	for( file = changes->files; file != NULL ; file = file->next ) {
		const char *name, *version, *p;
		size_t namelen, versionlen, l;
		bool_t has_tar, has_diff, has_orig;

		if( file->type != ft_DSC )
			continue;
		if( !strlist_in(&changes->architectures, "source") ) {
			fprintf(stderr,
"ERROR: '%s' contains a .dsc, but does not list Architecture 'source'!\n",
				changesfilename);
		}
		if( file->fullfilename == NULL ) {
			fprintf(stderr,
"ERROR: Could not find '%s'!\n", file->basename);
			continue;
		}
		if( file->dsc == NULL ) {
			fprintf(stderr,
"WARNING: Could not read '%s', thus it cannot be checked!\n", file->fullfilename);
			continue;
		}
		if( file->dsc->name == NULL )
			fprintf(stderr,
"ERROR: '%s' does not contain a 'Source:'-header!\n", file->fullfilename);
		else if( changes->name != NULL &&
				strcmp(changes->name, file->dsc->name) != 0 )
			fprintf(stderr,
"ERROR: '%s' lists Source '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				file->dsc->name, changes->name);
		if( file->dsc->version == NULL )
			fprintf(stderr,
"ERROR: '%s' does not contain a 'Version:'-header!\n", file->fullfilename);
		else if( changes->version != NULL &&
				strcmp(changes->version, file->dsc->version) != 0 )
			fprintf(stderr,
"ERROR: '%s' lists Version '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				file->dsc->version, changes->version);
		if( file->dsc->maintainer == NULL )
			fprintf(stderr,
"ERROR: No maintainer specified in '%s'!\n", file->fullfilename);
		else if( changes->maintainer != NULL &&
				strcmp(changes->maintainer, file->dsc->maintainer) != 0 )
			fprintf(stderr,
"Warning: '%s' lists Maintainer '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				file->dsc->maintainer, changes->maintainer);
		if( file->dsc->section != NULL && file->section != NULL &&
				strcmp(file->section, file->dsc->section) != 0 )
			fprintf(stderr,
"Warning: '%s' has Section '%s' while .changes says it is '%s'!\n",
				file->fullfilename,
				file->dsc->section, file->section);
		if( file->dsc->priority != NULL && file->priority != NULL &&
				strcmp(file->priority, file->dsc->priority) != 0 )
			fprintf(stderr,
"Warning: '%s' has Priority '%s' while .changes says it is '%s'!\n",
				file->fullfilename,
				file->dsc->priority, file->priority);
		// Todo: check types of files it contains...
		// check names are sensible
		p = file->basename;
		while( *p != '\0' && *p != '_' )
			p++;
		if( *p == '_' ) {
			l = strlen(p+1);
			assert( l >= 4 ); /* It ends in ".dsc" to come here */
		} else
			l = 0;

		if( file->dsc->name != NULL ) {
			name = file->dsc->name;
			namelen = strlen(name);
		} else {
			// TODO: more believe file name or changes name?
			if( changes->name != NULL ) {
				name = changes->name;
#ifdef STUPIDCC
				namelen = strlen(name);
#endif
			} else {
				if( *p != '_' ) {
					name = NULL;
					namelen = 0;
					fprintf(stderr,
"Warning: '%s' does not contain a '_' seperating name and version!\n",
						file->basename);
				}else {
					name = file->basename;
					namelen = p-name;
				}
			}
		}
		if( file->dsc->version != NULL ) {
			version = file->dsc->version;
			versionlen = strlen(version);
		} else {
			// TODO: dito
			if( changes->version != NULL ) {
				version = changes->version;
				versionlen = strlen(version);
			} else {
				if( *p != '_' ) {
					version = NULL;
#ifdef STUPIDCC
					versionlen = 0;
#endif
					if( name != NULL )
						fprintf(stderr,
"ERROR: '%s' does not contain a '_' seperating name and version!\n",
							file->basename);
				} else {
					version = p+1;
					versionlen = l-4;
				}
			}
		}
		if( name != NULL && version != NULL ) {
			if( *p != '_'
			|| (size_t)(p-file->basename) != namelen || l-4 != versionlen
			|| strncmp(p+1, version, versionlen) != 0
			|| strncmp(file->basename, name, namelen) != 0 )
				fprintf(stderr,
"ERROR: '%s' is not called '%*s_%*s.dsc' as expected!\n",
					file->basename,
					(unsigned int)namelen, name,
					(unsigned int)versionlen, version);
		}
		has_tar = FALSE;
		has_diff = FALSE;
		has_orig = FALSE;
		for( j = 0 ; j < file->dsc->filecount ; j++ ) {
			const struct sourcefile *f = &file->dsc->files[j];
			size_t expectedversionlen;

			switch( f->file->type ) {
				case ft_UNKNOWN:
					fprintf(stderr,
"ERROR: '%s' lists a file '%s' with unrecognized suffix!\n",
						file->fullfilename,
						f->basename);
					break;
				case ft_TAR_GZ: case ft_TAR_BZ2:
					if( has_tar || has_orig )
						fprintf(stderr,
"ERROR: '%s' lists multiple .tar files!\n",
						file->fullfilename);
					has_tar = TRUE;
					break;
				case ft_ORIG_TAR_GZ: case ft_ORIG_TAR_BZ2:
					if( has_tar || has_orig )
						fprintf(stderr,
"ERROR: '%s' lists multiple .tar files!\n",
						file->fullfilename);
					has_orig = TRUE;
					break;
				case ft_DIFF_GZ: case ft_DIFF_BZ2:
					if( has_diff )
						fprintf(stderr,
"ERROR: '%s' lists multiple .diff files!\n",
						file->fullfilename);
					has_diff = TRUE;
					break;
				default:
					assert( f->file->type == ft_UNKNOWN );
			}

			if( name == NULL ) // TODO: try extracting it from this
				continue;
			if( strncmp(f->file->basename, name, namelen) != 0
					|| f->file->basename[namelen] != '_' )
				fprintf(stderr,
"ERROR: '%s' does not begin with '%*s_' as expected!\n",
					f->file->basename,
					(unsigned int)namelen, name);

			if( version == NULL )
				continue;

			if(f->file->type == ft_ORIG_TAR_GZ
					|| f->file->type == ft_ORIG_TAR_BZ2) {
				const char *p, *revision;
				revision = NULL;
				for( p=version; *p != '\0'; p++ ) {
					if( *p == '-' )
						revision = p;
				}
				if( revision == NULL )
					expectedversionlen = versionlen;
				else
					expectedversionlen = revision - version;
			} else
				expectedversionlen = versionlen;

			if( strncmp(f->file->basename+namelen+1,
					version, expectedversionlen) != 0
				|| ( f->file->type != ft_UNKNOWN &&
					strcmp(f->file->basename+namelen+1
						+expectedversionlen,
					typesuffix[f->file->type]) != 0 ))
				fprintf(stderr,
"ERROR: '%s' is not called '%*s_%*s%s' as expected!\n",
					f->file->basename,
					(unsigned int)namelen, name,
					(unsigned int)expectedversionlen,
					version,
					typesuffix[f->file->type]);
		}
		if( !has_tar && !has_orig )
			if( has_diff )
				fprintf(stderr,
"ERROR: '%s' lists only a .diff, but no .orig.tar!\n",
						file->fullfilename);
			else
				fprintf(stderr,
"ERROR: '%s' lists no source files!\n",
						file->fullfilename);
		else if( has_diff && !has_orig )
			fprintf(stderr,
"ERROR: '%s' lists a .diff, but the .tar is not called .orig.tar!\n",
					file->fullfilename);
		else if( !has_diff && has_orig )
			fprintf(stderr,
"ERROR: '%s' lists a .orig.tar, but no .diff!\n",
					file->fullfilename);
	}
	printf("Checking Binary consistency...\n");
	for( j = 0 ; j < changes->binarycount ; j++ ) {
		struct binary *b = &changes->binaries[j];

		if( b->files == NULL && !b->uncheckable ) {
			/* no files - not even conjectured -, headers must be wrong */

			if( b->description != NULL && !b->missedinheader ) {
				fprintf(stderr,
"ERROR: '%s' has binary '%s' in 'Binary:' and 'Description:'-header, but no files for it found!\n",
					changesfilename, b->name);
			} else if( b->description != NULL) {
				fprintf(stderr,
"ERROR: '%s' has unexpected description of '%s'\n",
					changesfilename, b->name);
			} else {
				assert( !b->missedinheader );
				fprintf(stderr,
"ERROR: '%s' has unexpected Binary: '%s'\n",
					changesfilename, b->name);
			}
		}
		if( b->files == NULL )
			continue;
		/* files are there, make sure they are listed and
		 * have a description*/

		if( b->description == NULL ) {
			fprintf(stderr,
"ERROR: '%s' has no description for '%s'\n",
				changesfilename, b->name);
		}
		if( b->missedinheader ) {
				fprintf(stderr,
"ERROR: '%s' does not list '%s' in its Binary-header!\n",
					changesfilename, b->name);
		}
		// TODO: check if the files have the names they should
		// have an architectures as they are listed...
	}
	for( file = changes->files; file != NULL ; file = file->next ) {
		const struct binary *b;
		const struct binaryfile *deb;

		if( file->type != ft_DEB && file->type != ft_UDEB )
			continue;
		if( file->fullfilename == NULL ) {
			fprintf(stderr,
"ERROR: Could not find '%s'!\n", file->basename);
			continue;
		}
		if( file->deb == NULL ) {
			fprintf(stderr,
"WARNING: Could not read '%s', thus it cannot be checked!\n", file->fullfilename);
			continue;
		}
		deb = file->deb;
		b = deb->binary;

		if( deb->shortdescription == NULL )
			fprintf(stderr,
"Warning: '%s' contains no description!\n",
				file->fullfilename);
		else if( b->description != NULL &&
			 strcmp( b->description, deb->shortdescription) != 0 )
				fprintf(stderr,
"Warning: '%s' says '%s' has description '%s' while '%s' has '%s'!\n",
					changesfilename, b->name,
					b->description,
					file->fullfilename,
					deb->shortdescription);
		if( deb->name == NULL )
			fprintf(stderr,
"ERROR: '%s' does not contain a 'Package:'-header!\n", file->fullfilename);
		if( deb->sourcename != NULL ) {
			if( strcmp(changes->name, deb->sourcename) != 0 )
				fprintf(stderr,
"ERROR: '%s' lists Source '%s' while .changes lists '%s'!\n",
					file->fullfilename,
					deb->sourcename, changes->name);
		} else if( deb->name != NULL &&
				strcmp(changes->name, deb->name) != 0 ) {
			fprintf(stderr,
"ERROR: '%s' lists Source '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				deb->name, changes->name);
		}
		if( deb->version == NULL )
			fprintf(stderr,
"ERROR: '%s' does not contain a 'Version:'-header!\n", file->fullfilename);
		if( deb->sourceversion != NULL ) {
			if( strcmp(changes->version, deb->sourceversion) != 0 )
				fprintf(stderr,
"ERROR: '%s' lists Source version '%s' while .changes lists '%s'!\n",
					file->fullfilename,
					deb->sourceversion, changes->version);
		} else if( deb->version != NULL &&
				strcmp(changes->version, deb->version) != 0 ) {
			fprintf(stderr,
"ERROR: '%s' lists Source version '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				deb->version, changes->name);
		}

		if( deb->maintainer == NULL )
			fprintf(stderr,
"ERROR: No maintainer specified in '%s'!\n", file->fullfilename);
		else if( changes->maintainer != NULL &&
				strcmp(changes->maintainer, deb->maintainer) != 0 )
			fprintf(stderr,
"Warning: '%s' lists Maintainer '%s' while .changes lists '%s'!\n",
				file->fullfilename,
				deb->maintainer, changes->maintainer);
		if( deb->section == NULL )
			fprintf(stderr,
"ERROR: No section specified in '%s'!\n", file->fullfilename);
		else if( file->section != NULL &&
				strcmp(file->section, deb->section) != 0 )
			fprintf(stderr,
"Warning: '%s' has Section '%s' while .changes says it is '%s'!\n",
				file->fullfilename,
				deb->section, file->section);
		if( deb->priority == NULL )
			fprintf(stderr,
"ERROR: No priority specified in '%s'!\n", file->fullfilename);
		else if( file->priority != NULL &&
				strcmp(file->priority, deb->priority) != 0 )
			fprintf(stderr,
"Warning: '%s' has Priority '%s' while .changes says it is '%s'!\n",
				file->fullfilename,
				deb->priority, file->priority);
		verify_binary_name(file->basename, deb->name, deb->version,
				deb->architecture, file->type);
		if( deb->architecture != NULL
		&& !strlist_in(&changes->architectures,  deb->architecture) ) {
			fprintf(stderr,
"ERROR: '%s' does not list Architecture: '%s' needed for '%s'!\n",
				changesfilename, deb->architecture,
				file->fullfilename);
		}
		// todo: check for md5sums file, verify it...
	}

	printf("Checking md5sums...\n");
	r = getmd5sums(changes);
	if( RET_WAS_ERROR(r) )
		return r;
	for( file = changes->files; file != NULL ; file = file->next ) {

		if( file->fullfilename == NULL ) {
			fprintf(stderr, "WARNING: Could not check md5sum of '%s' as file not found!\n", file->basename);
			if( file->type == ft_DSC ) {
				fprintf(stderr, "WARNING: This file most likely contains other md5sums which could also not be checked because it was not found!\n");
			}
			continue;
		}
		if( file->changesmd5sum == NULL )
			/* nothing to check here */
			continue;

		if( file->realmd5sum == NULL ) {
			fprintf(stderr, "WARNING: Could not check md5sum of '%s'! File vanished while checking or not readable?\n", file->basename);
		} else if( strcmp(file->realmd5sum, file->changesmd5sum) != 0 ) {
			fprintf(stderr, "ERROR: md5sum of '%s' is %s instead of expected %s!\n",
					file->fullfilename,
					file->realmd5sum,
					file->changesmd5sum);
		}

		if( file->type == ft_DSC ) {
			unsigned int i;

			if( file->dsc == NULL ) {
				fprintf(stderr,
"WARNING: Could not read '%s', thus the content cannot be checked\n"
" and may be faulty and other things depending on it may be incorrect!\n", file->basename);
				continue;
			}

			for( i = 0 ; i < file->dsc->filecount ; i++ ) {
				struct sourcefile *f = &file->dsc->files[i];

				assert( f->expectedmd5sum != NULL );
				verify_sourcefile_md5sums(f,file->fullfilename);
			}
		}
		// TODO: check .deb files
	}
	return RET_OK;
}

static bool_t isarg(int argc, char **argv, const char *name) {
	while( argc > 0 ) {
		if( strcmp(*argv, name) == 0 )
			return TRUE;
		argc--;
		argv++;
	}
	return FALSE;
}

static retvalue updatemd5sums(const char *changesfilename, struct changes *c, int argc, char **argv) {
	retvalue r;
	struct fileentry *file;

	r = getmd5sums(c);
	if( RET_WAS_ERROR(r) )
		return r;
	/* first update all .dsc files and perhaps recalculate their md5sums */
	for( file = c->files; file != NULL ; file = file->next ) {
		unsigned int i;

		if( file->type != ft_DSC )
			continue;

		if( file->dsc == NULL ) {
			fprintf(stderr,
"WARNING: Could not read '%s', hopeing the content and its md5sum are correct!\n",
					file->basename);
			continue;
		}
		assert( file->fullfilename != NULL );
		for( i = 0 ; i < file->dsc->filecount ; i++ ) {
			struct sourcefile *f = &file->dsc->files[i];
			bool_t doit;
			char *md5sum = NULL;
			const char *realmd5;

			assert( f->expectedmd5sum != NULL );
			assert( f->basename != NULL );

			doit = isarg(argc,argv,f->basename);
			if( argc > 0 && !doit )
				continue;

			if( f->file == NULL || f->file->changesmd5sum == NULL ) {
				if( !doit ) {
					fprintf(stderr,
"Not checking '%s' as not in .changes and not specified on command line.\n",
						f->basename);
					continue;
				}
				// ... get md5sum ..;
				realmd5 = md5sum;
			} else {
				if( f->file->realmd5sum == NULL ) {
					fprintf(stderr, "WARNING: Could not check md5sum of '%s'!\n", f->basename);
					continue;
				}
				realmd5 = f->file->realmd5sum;
			}

			if( strcmp(f->expectedmd5sum, realmd5) == 0 ) {
				/* already correct */
				free(md5sum);
				continue;
			}
			fprintf(stderr,
"Going to update '%s' in '%s'\nfrom '%s'\nto   '%s'.\n",
					f->basename, file->fullfilename,
					realmd5, f->expectedmd5sum);
			free(f->expectedmd5sum);
			if( md5sum != NULL )
				f->expectedmd5sum = md5sum;
			else {
				free(md5sum);
				f->expectedmd5sum = strdup(realmd5);
				if( f->expectedmd5sum == NULL )
					return RET_ERROR_OOM;
			}
			file->dsc->modified = TRUE;
		}
		if( file->dsc->modified ) {
			r = write_dsc_file(file, DSC_WRITE_FILES);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	for( file = c->files; file != NULL ; file = file->next ) {

		if( file->changesmd5sum == NULL )
			/* nothing to check here */
			continue;
		if( file->realmd5sum == NULL ) {
			fprintf(stderr, "WARNING: Could not check md5sum of '%s'! Leaving it as it is.\n", file->basename);
			continue;
		}
		if( strcmp(file->realmd5sum, file->changesmd5sum) == 0 ) {
			continue;
		}
		fprintf(stderr,
"Going to update '%s' in '%s'\nfrom '%s'\nto   '%s'.\n",
					file->basename, changesfilename,
					file->realmd5sum, file->changesmd5sum);
		free(file->changesmd5sum);
		file->changesmd5sum = strdup(file->realmd5sum);
		if( file->changesmd5sum == NULL )
			return RET_ERROR_OOM;
		c->modified = TRUE;
	}
	if( c->modified ) {
		return write_changes_file(changesfilename, c, CHANGES_WRITE_FILES);
	} else
		return RET_NOTHING;
}

static retvalue includeallsources(const char *changesfilename, struct changes *c, int argc, char **argv) {
	struct fileentry *file;

	for( file = c->files; file != NULL ; file = file->next ) {
		unsigned int i;

		if( file->type != ft_DSC )
			continue;

		if( file->dsc == NULL ) {
			fprintf(stderr,
"WARNING: Could not read '%s', thus cannot determine if it depends on unlisted files!\n",
					file->basename);
			continue;
		}
		assert( file->fullfilename != NULL );
		for( i = 0 ; i < file->dsc->filecount ; i++ ) {
			struct sourcefile *f = &file->dsc->files[i];

			assert( f->expectedmd5sum != NULL );
			assert( f->basename != NULL );
			assert( f->file != NULL );

			if( f->file->changesmd5sum != NULL )
				continue;

			if( argc > 0 && !isarg(argc,argv,f->basename) )
				continue;

			f->file->changesmd5sum = strdup(f->expectedmd5sum);
			if( f->file->changesmd5sum == NULL )
				return RET_ERROR_OOM;
			/* copy section and priority information from the dsc */
			if( f->file->section == NULL && file->section != NULL ) {
				f->file->section = strdup(file->section);
				if( f->file->section == NULL )
					return RET_ERROR_OOM;
			}
			if( f->file->priority == NULL && file->priority != NULL ) {
				f->file->priority = strdup(file->priority);
				if( f->file->priority == NULL )
					return RET_ERROR_OOM;
			}

			fprintf(stderr,
"Going to add '%s' with '%s' to '%s'.\n",
					f->basename, f->expectedmd5sum,
					changesfilename);
			c->modified = TRUE;
		}
	}
	if( c->modified ) {
		return write_changes_file(changesfilename, c, CHANGES_WRITE_FILES);
	} else
		return RET_NOTHING;
}

static int execute_command(int argc, char **argv, const char *changesfilename, bool_t file_exists, struct changes *changesdata) {
	const char *command = argv[0];
	retvalue r;

	assert( argc > 0 );

	if( strcasecmp(command, "verify") == 0 ) {
		if( argc > 1 ) {
			fprintf(stderr, "Too many argument!\n");
			r = RET_ERROR;
		} else if( file_exists )
			r = verify(changesfilename, changesdata);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if( strcasecmp(command, "updatechecksums") == 0 ) {
		if( file_exists )
			r = updatemd5sums(changesfilename, changesdata, argc-1, argv+1);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else if( strcasecmp(command, "includeallsources") == 0 ) {
		if( file_exists )
			r = includeallsources(changesfilename, changesdata, argc-1, argv+1);
		else {
			fprintf(stderr, "No such file '%s'!\n",
					changesfilename);
			r = RET_ERROR;
		}
	} else {
		fprintf(stderr, "Unknown command '%s'\n", command);
		r = RET_ERROR;
	}
	return r;
}

int main(int argc,char *argv[]) {
	static const struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"ignore", required_argument, NULL, 'i'},
		{NULL, 0, NULL, 0},
	};
	int c;
	const char *changesfilename;
	bool_t file_exists;
	struct strlist validkeys,keys;
	struct changes *changesdata;
	retvalue r;

	init_ignores();


	while( (c = getopt_long(argc,argv,"+hi:",longopts,NULL)) != -1 ) {
		switch( c ) {
			case 'h':
				about(TRUE);
			case 'i':
				set_ignore(optarg,FALSE,CONFIG_OWNER_CMDLINE);
				break;
		}
	}
	if( argc <= 2 ) {
		about(FALSE);
	}
	signature_init(FALSE);

	changesfilename = argv[1];
	if( strcmp(changesfilename,"-") != 0 && !endswith(changesfilename,".changes")
			&& !IGNORING_(extension,
				"first argument does not ending with '.changes'\n") )
		exit(EXIT_FAILURE);
	file_exists = isregularfile(changesfilename);
	if( file_exists ) {
		char *changes;

		r = signature_readsignedchunk(changesfilename, &changes, &validkeys, &keys, NULL);
		if( !RET_IS_OK(r) ) {
			signatures_done();
			if( r == RET_ERROR_OOM )
				fprintf(stderr, "Out of memory!\n");
			exit(EXIT_FAILURE);
		}
		r = parse_changes(changesfilename, changes, &changesdata, NULL);
		if( RET_IS_OK(r) )
			changesdata->control = changes;
		else
			free(changes);
	} else {
		strlist_init(&keys);
		strlist_init(&validkeys);
		changesdata = calloc(1,sizeof(struct changes));
		if( changesdata == NULL )
			r = RET_ERROR_OOM;
		else {
			r = RET_OK;
		}
	}

	if( !RET_WAS_ERROR(r) ) {
		argc -= 2;
		argv += 2;
		r = execute_command(argc,argv, changesfilename, file_exists, changesdata);
	}
	changes_free(changesdata);
	strlist_done(&keys);

	signatures_done();
	if( RET_IS_OK(r) )
		exit(EXIT_SUCCESS);
	if( r == RET_ERROR_OOM )
		fprintf(stderr, "Out of memory!\n");
	exit(EXIT_FAILURE);
}
