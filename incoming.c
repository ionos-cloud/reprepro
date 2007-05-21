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
#include <dirent.h>
#include "error.h"
#include "ignore.h"
#include "mprintf.h"
#include "strlist.h"
#include "dirs.h"
#include "names.h"
#include "md5sum.h"
#include "chunks.h"
#include "copyfile.h"
#include "target.h"
#include "signature.h"
#include "binaries.h"
#include "sources.h"
#include "dpkgversions.h"
#include "uploaderslist.h"
#include "guesscomponent.h"
#include "log.h"
#include "override.h"
#include "tracking.h"
#include "incoming.h"
#include "changes.h"

extern int verbose;

struct incoming {
	/* by incoming_parse: */
	char *directory;
	char *tempdir;
	struct strlist allow;
	struct distribution **allow_into;
	struct distribution *default_into;
	/* by incoming_prepare: */
	struct strlist files;
	bool_t *processed;
	bool_t *delete;
	struct strlist md5sums;
	struct {
		/* do not error out on unused files */
		bool_t unused_files:1;
		/* allow .changes file to specify multipe distributions */
		bool_t multiple_distributions:1;
	} permit;
	struct {
		/* delete everything referenced by a .changes file
		 * when it is not accepted */
		bool_t on_deny:1;
		/* check owner when deleting on_deny */
		bool_t on_deny_check_owner:1;
		/* delete everything referenced by a .changes on errors
		 * after accepting that .changes file*/
		bool_t on_error:1;
		/* delete unused files after sucessfully
		 * processing the used ones */
		bool_t unused_files:1;
	} cleanup;
};
#define BASENAME(i,ofs) (i)->files.values[ofs]

static void incoming_free(/*@only@*/ struct incoming *i) {
	if( i == NULL )
		return;
	free(i->tempdir);
	free(i->directory);
	strlist_done(&i->allow);
	free(i->allow_into);
	strlist_done(&i->files);
	free(i->processed);
	free(i->delete);
	strlist_done(&i->md5sums);
	free(i);
}

static retvalue incoming_prepare(struct incoming *i) {
	DIR *dir;
	struct dirent *ent;
	retvalue r;
	int ret;

	/* TODO: decide whether to clean this directory first ... */
	r = dirs_make_recursive(i->tempdir);
	if( RET_WAS_ERROR(r) )
		return r;
	dir = opendir(i->directory);
	if( dir == NULL ) {
		int e = errno;
		fprintf(stderr, "Cannot scan '%s': %s\n", i->directory, strerror(e));
		return RET_ERRNO(e);
	}
	while( (ent = readdir(dir)) != NULL ) {
		if( ent->d_name[0] == '.' )
			continue;
		/* this should be impossible to hit.
		 * but given utf-8 encoding filesystems and
		 * overlong slashes, better check than be sorry */
		if( strchr(ent->d_name, '/') != NULL )
			continue;
		r = strlist_add_dup(&i->files, ent->d_name) ;
		if( RET_WAS_ERROR(r) ) {
			closedir(dir);
			return r;
		}
	}
	ret = closedir(dir);
	if( ret != 0 ) {
		int e = errno;
		fprintf(stderr, "Error scaning '%s': %s\n", i->directory, strerror(e));
		return RET_ERRNO(e);
	}
	r = strlist_init_n(i->files.count,&i->md5sums);
	if( RET_WAS_ERROR(r) )
		return r;
	i->processed = calloc(i->files.count,sizeof(bool_t));
	if( i->processed == NULL )
		return RET_ERROR_OOM;
	i->delete = calloc(i->files.count,sizeof(bool_t));
	if( i->delete == NULL )
		return RET_ERROR_OOM;
	return RET_OK;
}

struct importsparsedata {
	char *filename;
	/*@temp@*/const char *name;
	/*@temp@*/struct distribution *distributions;
	struct incoming *i;
	/*@temp@*/const char *basedir;
};

static retvalue translate(struct distribution *distributions, struct strlist *names, struct distribution ***r) {
	struct distribution **d;
	int j;

	d = calloc(names->count,sizeof(struct distribution*));
	if( d == NULL )
		return RET_ERROR_OOM;
	for( j = 0 ; j < names->count ; j++ ) {
		d[j] = distribution_find(distributions, names->values[j]);
		if( d[j] == NULL ) {
			free(d);
			return RET_ERROR;
		}
	}
	*r = d;
	return RET_OK;
}

static retvalue incoming_parse(void *data, const char *chunk) {
	char *name;
	struct incoming *i;
	struct importsparsedata *d = data;
	retvalue r;
	struct strlist allowlist, allow_into;
	char *default_into;
	static const char * const allowedfields[] = {"Name", "TempDir",
		"IncomingDir", "Default", "Allow", "Multiple",
		NULL};

	r = chunk_getvalue(chunk, "Name", &name);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Expected 'Name' header in every chunk in '%s'!\n", d->filename);
		return RET_ERROR_MISSING;
	}
	if( strcmp(name, d->name) != 0 ) {
		free(name);
		return RET_NOTHING;
	}
	free(name);

	r = chunk_checkfields(chunk, allowedfields, TRUE);
	if( RET_WAS_ERROR(r) )
		return r;

	i = calloc(1,sizeof(struct incoming));
	if( i == NULL )
		return RET_ERROR_OOM;

	r = chunk_getvalue(chunk, "TempDir", &i->tempdir);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Expected 'TempDir' header not found in definition for '%s' in '%s'!\n", d->name, d->filename);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		incoming_free(i);
		return r;
	}
	if( i->tempdir[0] != '/' ) {
		char *n = calc_dirconcat(d->basedir, i->tempdir);
		if( n == NULL ) {
			incoming_free(i);
			return RET_ERROR_OOM;
		}
		free(i->tempdir);
		i->tempdir = n;
	}
	r = chunk_getvalue(chunk, "IncomingDir", &i->directory);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Expected 'IncomingDir' header not found in definition for '%s' in '%s'!\n", d->name, d->filename);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		incoming_free(i);
		return r;
	}
	if( i->directory[0] != '/' ) {
		char *n = calc_dirconcat(d->basedir, i->directory);
		if( n == NULL ) {
			incoming_free(i);
			return RET_ERROR_OOM;
		}
		free(i->directory);
		i->directory = n;
	}
	r = chunk_getvalue(chunk, "Default", &default_into);
	if( RET_WAS_ERROR(r) ) {
		incoming_free(i);
		return r;
	}
	if( RET_IS_OK(r) ) {
		i->default_into = distribution_find(d->distributions, default_into);
		if( i->default_into == NULL ) {
			free(default_into);
			incoming_free(i);
			return RET_ERROR;
		}
		free(default_into);
	} else
		i->default_into = NULL;

	r = chunk_getwordlist(chunk, "Allow", &allowlist);
	if( RET_WAS_ERROR(r) ) {
		incoming_free(i);
		return r;
	}
	if( r == RET_NOTHING ) {
		if( i->default_into == NULL ) {
			fprintf(stderr, "'%s' in '%s' has neither a 'Allow' nor a 'Default' definition!\nAborting as nothing would be let in.\n", d->name, d->filename);
			incoming_free(i);
			return RET_ERROR;
		}
		strlist_init(&i->allow);
		i->allow_into = NULL;
	} else {
		r = splitlist(&i->allow,&allow_into, &allowlist);
		strlist_done(&allowlist);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			incoming_free(i);
			return r;
		}
		assert( i->allow.count == allow_into.count );
		r = translate(d->distributions, &allow_into, &i->allow_into);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&allow_into);
			incoming_free(i);
			return r;
		}
		strlist_done(&allow_into);
	}
	r = chunk_gettruth(chunk, "Multiple");
	if( RET_WAS_ERROR(r) ) {
		incoming_free(i);
		return r;
	}
	i->permit.multiple_distributions = RET_IS_OK(r);
	d->i = i;
	return RET_OK;
}

static retvalue incoming_init(const char *basedir,const char *confdir, struct distribution *distributions, const char *name, /*@out@*/struct incoming **result) {
	retvalue r;
	struct importsparsedata imports;

	imports.name = name;
	imports.distributions = distributions;
	imports.i = NULL;
	imports.basedir = basedir;
	imports.filename = calc_dirconcat(confdir, "incoming");
	if( imports.filename == NULL )
		return RET_ERROR_OOM;

	r = chunk_foreach(imports.filename, incoming_parse, &imports, TRUE);
	if( r == RET_NOTHING ) {
		fprintf(stderr, "No definition for '%s' found in '%s'!\n",
				name, imports.filename);
		r = RET_ERROR_MISSING;
	}
	free(imports.filename);
	if( RET_WAS_ERROR(r) )
		return r;

	r = incoming_prepare(imports.i);
	if( RET_WAS_ERROR(r) ) {
		incoming_free(imports.i);
		return r;
	}
	*result = imports.i;
	return r;
}

struct candidate {
	/* from candidate_read */
	int ofs;
	char *fullfilename;
	char *control;
	struct strlist keys;
	/* from candidate_parse */
	char *source, *version;
	struct strlist distributions,
		       architectures,
		       binaries;
	struct candidate_file {
		/* set by _addfileline */
		struct candidate_file *next;
		int ofs; /* to basename in struct incoming->files */
		filetype type;
		/* all NULL if it is the .changes itself,
		 * otherwise the data from the .changes for this file: */
		char *md5sum;
		char *section;
		char *priority;
		char *architecture;
		char *name;
		/* set later */
		bool_t used;
		char *tempfilename;
		/* distribution-unspecific contents of the packages */
		/* - only for FE_BINARY types: */
		struct deb_headers deb;
		/* - only for fe_DSC types */
		struct dsc_headers dsc;
	} *files;
	struct candidate_perdistribution {
		struct candidate_perdistribution *next;
		struct distribution *into;
		struct candidate_package {
			/* a package is something installing files, including
			 * the pseudo-package for the .changes file, if that is
			 * to be included */
			struct candidate_package *next;
			const struct candidate_file *master;
			char *component;
			struct strlist filekeys;
			/* a list of pointers to the files belonging to those
			 * filekeys, NULL if it does not need linking/copying */
			const struct candidate_file **files;
			/* only for FE_PACKAGE: */
			char *control;
			/* only for fe_DSC */
			char *directory;
		} *packages;
	} *perdistribution;
};

static void candidate_file_free(/*@only@*/struct candidate_file *f) {
	free(f->md5sum);
	free(f->section);
	free(f->priority);
	free(f->architecture);
	free(f->name);
	if( FE_BINARY(f->type) )
		binaries_debdone(&f->deb);
	if( f->type == fe_DSC )
		sources_done(&f->dsc);
	if( f->tempfilename != NULL ) {
		unlink(f->tempfilename);
		free(f->tempfilename);
		f->tempfilename = NULL;
	}
	free(f);
}

static void candidate_package_free(/*@only@*/struct candidate_package *p) {
	free(p->control);
	free(p->component);
	free(p->directory);
	strlist_done(&p->filekeys);
	free(p->files);
	free(p);
}

static void candidate_free(/*@only@*/struct candidate *c) {
	if( c == NULL )
		return;
	free(c->fullfilename);
	free(c->control);
	strlist_done(&c->keys);
	free(c->source);
	free(c->version);
	strlist_done(&c->distributions);
	strlist_done(&c->architectures);
	strlist_done(&c->binaries);
	while( c->perdistribution != NULL ) {
		struct candidate_perdistribution *d = c->perdistribution;
		c->perdistribution = d->next;

		while( d->packages != NULL ) {
			struct candidate_package *p = d->packages;
			d->packages = p->next;
			candidate_package_free(p);
		}
		free(d);
	}
	while( c->files != NULL ) {
		struct candidate_file *f = c->files;
		c->files = f->next;
		candidate_file_free(f);
	}
	free(c);
}

static retvalue candidate_newdistribution(struct candidate *c, struct distribution *distribution) {
	struct candidate_perdistribution *n,**pp = &c->perdistribution;

	while( *pp != NULL ) {
		if( (*pp)->into == distribution )
			return RET_NOTHING;
		pp = &(*pp)->next;
	}
	n = calloc(1, sizeof(struct candidate_perdistribution));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->into = distribution;
	*pp = n;
	return RET_OK;
}

static struct candidate_package *candidate_newpackage(struct candidate_perdistribution *fordistribution, const struct candidate_file *master) {
	struct candidate_package *n,**pp = &fordistribution->packages;

	while( *pp != NULL )
		pp = &(*pp)->next;
	n = calloc(1, sizeof(struct candidate_package));
	if( n != NULL ) {
		n->master = master;
		*pp = n;
	}
	return n;
}

static retvalue candidate_read(struct incoming *i, int ofs, struct candidate **result, bool_t *broken) {
	struct candidate *n;
	retvalue r;

	n = calloc(1,sizeof(struct candidate));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->ofs = ofs;
	n->fullfilename = calc_dirconcat(i->directory, BASENAME(i,ofs));
	if( n->fullfilename == NULL ) {
		free(n);
		return RET_ERROR_OOM;
	}
	r = signature_readsignedchunk(n->fullfilename, &n->control, &n->keys, NULL, broken);
	if( RET_WAS_ERROR(r) ) {
		free(n->fullfilename);
		free(n);
		return r;
	}
	/* first file of any .changes file is the file itself */
	n->files = calloc(1,sizeof(struct candidate_file));
	if( n->files == NULL ) {
		candidate_free(n);
		return RET_ERROR_OOM;
	}
	n->files->ofs = n->ofs;
	n->files->type = fe_UNKNOWN;
	n->files->used = TRUE;

	assert( RET_IS_OK(r) );
	*result = n;
	return RET_OK;
}

static retvalue candidate_addfileline(struct incoming *i, struct candidate *c, const char *fileline) {
	struct candidate_file **p, *n;
	char *basename;
	retvalue r;

	n = calloc(1,sizeof(struct candidate_file));
	if( n == NULL )
		return RET_ERROR_OOM;

	r = changes_parsefileline(fileline, &n->type, &basename, &n->md5sum,
			&n->section, &n->priority, &n->architecture, &n->name);
	if( RET_WAS_ERROR(r) ) {
		free(n);
		return r;
	}
	n->ofs = strlist_ofs(&i->files, basename);
	if( n->ofs < 0 ) {
		fprintf(stderr,"In '%s': file '%s' not found in the incoming dir!\n", i->files.values[c->ofs], basename);
		free(basename);
		candidate_file_free(n);
		return RET_ERROR_MISSING;
	}
	free(basename);

	p = &c->files;
	while( *p != NULL )
		p = &(*p)->next;
	*p = n;
	return RET_OK;
}

static retvalue candidate_parse(struct incoming *i, struct candidate *c) {
	retvalue r;
	struct strlist filelines;
	int j;
#define R if( RET_WAS_ERROR(r) ) return r;
#define E(err) { \
		if( r == RET_NOTHING ) { \
			fprintf(stderr,"In '%s': " err "\n",BASENAME(i,c->ofs)); \
			r = RET_ERROR; \
	  	} \
		if( RET_WAS_ERROR(r) ) return r; \
	}
	r = chunk_getname(c->control, "Source", &c->source, FALSE);
	E("Missing 'Source' field!");
	r = propersourcename(c->source);
	E("Malforce Source name!");
	r = chunk_getwordlist(c->control,"Binary",&c->binaries);
	E("Missing 'Binary' field!");
	r = chunk_getwordlist(c->control,"Architecture",&c->architectures);
	E("Missing 'Architecture' field!");
	r = chunk_getvalue(c->control,"Version",&c->version);
	E("Missing 'Version' field!");
	r = properversion(c->version);
	E("Malforce Version number!");
	r = chunk_getwordlist(c->control,"Distribution",&c->distributions);
	E("Missing 'Distribution' field!");
	r = chunk_getextralinelist(c->control,"Files",&filelines);
	E("Missing 'Files' field!");
	for( j = 0 ; j < filelines.count ; j++ ) {
		r = candidate_addfileline(i, c, filelines.values[j]);
		if( RET_WAS_ERROR(r) ) {
			strlist_done(&filelines);
			return r;
		}
	}
	strlist_done(&filelines);
	if( c->files == NULL || c->files->next == NULL ) {
		fprintf(stderr,"In '%s': Empty 'Files' section!\n",
				BASENAME(i,c->ofs));
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue candidate_earlychecks(struct incoming *i, struct candidate *c) {
	struct candidate_file *file;
	retvalue r;

	// TODO: allow being more permissive,
	// that will need some more checks later, though
	r = propersourcename(c->source);
	if( RET_WAS_ERROR(r) )
		return r;
	r = properversion(c->version);
	if( RET_WAS_ERROR(r) )
		return r;
	for( file = c->files ; file != NULL ; file = file->next ) {
		if( !FE_PACKAGE(file->type) )
			continue;
		if( strlist_in(&c->architectures, file->architecture) )
			continue;
		fprintf(stderr, "'%s' is not listed in the Architecture header of '%s' but file '%s' looks like it!\n",
				file->architecture, BASENAME(i,c->ofs),
				BASENAME(i,file->ofs));
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue candidate_usefile(const struct incoming *i,const struct candidate *c,struct candidate_file *file) {
	const char *basename;
	char *origfile,*tempfilename;
	retvalue r;
	const char *p;

	if( file->used && file->tempfilename != NULL )
		return RET_OK;
	assert(file->tempfilename == NULL);
	basename = BASENAME(i,file->ofs);
	for( p = basename; *p != '\0' ; p++ ) {
		if( (0x80 & *(const unsigned char *)p) != 0 ) {
			fprintf(stderr, "Invalid filename '%s' listed in '%s': contains 8-bit characters\n", basename, BASENAME(i,c->ofs));
			return RET_ERROR;
		}
	}
	tempfilename = calc_dirconcat(i->tempdir, basename);
	if( tempfilename == NULL )
		return RET_ERROR_OOM;
	origfile = calc_dirconcat(i->directory, basename);
	if( origfile == NULL ) {
		free(tempfilename);
		return RET_ERROR_OOM;
	}
	unlink(tempfilename);
	r = copy(tempfilename, origfile, file->md5sum, (file->md5sum==NULL)?&file->md5sum:NULL);
	free(origfile);
	if( RET_WAS_ERROR(r) ) {
		free(tempfilename);
		// ...
		return r;
	}
	file->tempfilename = tempfilename;
	file->used = TRUE;
	return RET_OK;

}

static inline retvalue getsectionprioritycomponent(const struct incoming *i,const struct candidate *c,const struct distribution *into,const struct candidate_file *file, const char *name, const struct overrideinfo *oinfo, /*@out@*/const char **section_p, /*@out@*/const char **priority_p, /*@out@*/char **component) {
	retvalue r;
	const char *section, *priority;

	section = override_get(oinfo, SECTION_FIELDNAME);
	if( section == NULL ) {
		// TODO: warn about disparities here?
		section = file->section;
	}
	if( section == NULL || strcmp(section,"-") == 0 ) {
		fprintf(stderr, "No section found for '%s' ('%s' in '%s')!\n",
				name,
				BASENAME(i,file->ofs), BASENAME(i,c->ofs));
		return RET_ERROR;
	}
	priority = override_get(oinfo, PRIORITY_FIELDNAME);
	if( priority == NULL ) {
		// TODO: warn about disparities here?
		priority = file->priority;
	}
	if( priority == NULL || strcmp(priority,"-") == 0 ) {
		fprintf(stderr, "No priority found for '%s' ('%s' in '%s')!\n",
				name,
				BASENAME(i,file->ofs), BASENAME(i,c->ofs));
		return RET_ERROR;
	}

	r = guess_component(into->codename,&into->components,BASENAME(i,file->ofs),section,NULL,component);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	*section_p = section;
	*priority_p = priority;
	return RET_OK;
}

static retvalue candidate_read_deb(struct incoming *i,struct candidate *c,struct candidate_file *file) {
	retvalue r;

	r = binaries_readdeb(&file->deb, file->tempfilename, TRUE);
	if( RET_WAS_ERROR(r) )
		return r;
	if( strcmp(file->name, file->deb.name) != 0 ) {
		// TODO: add permissive thing to ignore this
		fprintf(stderr, "Name part of filename ('%s') and name within the file ('%s') do not match for '%s' in '%s'!\n",
				file->name, file->deb.name,
				BASENAME(i,file->ofs), BASENAME(i,c->ofs));
		return RET_ERROR;
	}
	if( strcmp(file->architecture, file->deb.architecture) != 0 ) {
		// TODO: add permissive thing to ignore this in some cases
		// but do not forget to look into into->architectures then
		fprintf(stderr, "Architecture '%s' of '%s' does not match '%s' specified in '%s'!\n",
				file->deb.architecture, BASENAME(i,file->ofs),
				file->architecture, BASENAME(i,c->ofs));
		return RET_ERROR;
	}
	if( strcmp(c->source, file->deb.source) != 0 ) {
		// TODO: add permissive thing to ignore this
		// (beware if tracking is active)
		fprintf(stderr, "Source-header '%s' of '%s' and source name '%s' within the file '%s' do not match!\n",
				c->source, BASENAME(i,c->ofs),
				file->deb.source, BASENAME(i,file->ofs));
		return RET_ERROR;
	}
	if( strcmp(c->version, file->deb.sourceversion) != 0 ) {
		// TODO: add permissive thing to ignore this
		// (beware if tracking is active)
		fprintf(stderr, "Version-header '%s' of '%s' and source version '%s' within the file '%s' do not match!\n",
				c->version, BASENAME(i,c->ofs),
				file->deb.sourceversion, BASENAME(i,file->ofs));
		return RET_ERROR;
	}
	if( ! strlist_in(&c->binaries, file->deb.name) ) {
		fprintf(stderr, "Name '%s' of binary '%s' is not listed in Binaries-header of '%s'!\n",
				file->deb.name, BASENAME(i,file->ofs),
				BASENAME(i,c->ofs));
		return RET_ERROR;
	}
	r = properpackagename(file->deb.name);
	if( RET_IS_OK(r) )
		r = propersourcename(file->deb.source);
	if( RET_IS_OK(r) )
		r = properversion(file->deb.version);
	if( RET_IS_OK(r) )
		r = properfilenamepart(file->deb.architecture);
	if( RET_WAS_ERROR(r) )
		return r;
	return RET_OK;
}

static retvalue candidate_read_dsc(struct incoming *i,struct candidate *c,struct candidate_file *file) {
	retvalue r;
	bool_t broken = FALSE;
	char *p;

	r = sources_readdsc(&file->dsc, file->tempfilename, &broken);
	if( RET_WAS_ERROR(r) )
		return r;
	p = calc_source_basename(file->dsc.name,
			file->dsc.version);
	if( p == NULL )
		return RET_ERROR_OOM;
	r = strlist_include(&file->dsc.basenames, p);
	if( RET_WAS_ERROR(r) )
		return r;
	p = strdup(file->md5sum);
	if( p == NULL )
		return RET_ERROR_OOM;
	r = strlist_include(&file->dsc.md5sums, p);
	if( RET_WAS_ERROR(r) )
		return r;
	// TODO: take a look at "broken"...
	return RET_OK;
}

static retvalue candidate_read_files(struct incoming *i, struct candidate *c) {
	struct candidate_file *file;
	retvalue r;

	for( file = c->files ; file != NULL ; file = file->next ) {
		if( file->section != NULL &&
				strcmp(file->section, "byhand") == 0 ) {
			/* to avoid further tests for this file */
			file->type = fe_UNKNOWN;
			continue;
		}
		if( !FE_PACKAGE(file->type) )
			continue;
		r = candidate_usefile(i, c, file);
		if( RET_WAS_ERROR(r) )
			return r;
		assert(file->tempfilename != NULL);

		if( FE_BINARY(file->type) )
			r = candidate_read_deb(i, c, file);
		else if( file->type == fe_DSC )
			r = candidate_read_dsc(i, c, file);
		else {
			r = RET_ERROR;
			assert( FE_BINARY(file->type) || file->type == fe_DSC );
		}
		if( RET_WAS_ERROR(r) )
			return r;
	}
	return RET_OK;
}

static retvalue candidate_preparechangesfile(filesdb filesdb,const struct incoming *i,const struct candidate *c,struct candidate_perdistribution *per) {
	retvalue r;
	char *basename, *filekey;
	struct candidate_package *package;
	struct candidate_file *file;
	const char *component = NULL;
	assert( c->files != NULL && c->files->ofs == c->ofs );

	/* search for a component to use */
	for( package = per->packages ; package != NULL ; package = package->next ) {
		if( package->component != NULL ) {
			component = package->component;
			break;
		}
	}
	if( component == NULL )
		component = "strange";

	/* the .changes file is the first of its own files */
	file = c->files;

	/* copy the .changes file, to get its md5sum and be sure it is
	 * still there */
	r = candidate_usefile(i, c, file);
	if( RET_WAS_ERROR(r) )
		return r;
	assert( file->md5sum != NULL );

	package = candidate_newpackage(per, c->files);
	if( package == NULL )
		return RET_ERROR_OOM;

	basename = calc_changes_basename(c->source, c->version, &c->architectures);
	if( basename == NULL )
		return RET_ERROR_OOM;

	filekey = calc_filekey(component, c->source, basename);
	free(basename);
	if( filekey == NULL )
		return RET_ERROR_OOM;

	r = strlist_init_singleton(filekey, &package->filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	assert( package->filekeys.count == 1 );
	filekey = package->filekeys.values[0];
	package->files = calloc(1, sizeof(struct candidate_file *));
	if( package->files == NULL )
		return RET_ERROR_OOM;
	r = files_ready(filesdb, filekey, file->md5sum);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) )
		package->files[0] = file;
	return RET_OK;
}

static retvalue prepare_deb(filesdb filesdb,const struct incoming *i,const struct candidate *c,struct candidate_perdistribution *per,const struct candidate_file *file) {
	const char *section,*priority, *filekey;
	const struct overrideinfo *oinfo;
	struct candidate_package *package;
	const struct distribution *into = per->into;
	retvalue r;

	assert( FE_BINARY(file->type) );
	assert( file->tempfilename != NULL );
	assert( file->deb.name != NULL );

	package = candidate_newpackage(per, file);
	if( package == NULL )
		return RET_ERROR_OOM;
	assert( file == package->master );

	oinfo = override_search(file->type==fe_UDEB?into->overrides.udeb:
			                    into->overrides.deb,
	                        file->name);

	r = getsectionprioritycomponent(i,c,into,file,
			file->name, oinfo,
			&section, &priority, &package->component);
	if( RET_WAS_ERROR(r) )
		return r;

	if( file->type == fe_UDEB &&
	    !strlist_in(&into->udebcomponents, package->component)) {
		fprintf(stderr,
"Cannot put file '%s' of '%s' into component '%s',\n"
"as it is not listed in UDebComponents of '%s'!\n",
			BASENAME(i,file->ofs), BASENAME(i,c->ofs),
			package->component, into->codename);
		return RET_ERROR;
	}
	r = binaries_calcfilekeys(package->component, &file->deb,
			(package->master->type==fe_DEB)?"deb":"udeb",
			&package->filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	assert( package->filekeys.count == 1 );
	filekey = package->filekeys.values[0];
	package->files = calloc(1, sizeof(struct candidate_file *));
	if( package->files == NULL )
		return RET_ERROR_OOM;
	r = files_ready(filesdb, filekey, file->md5sum);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) )
		package->files[0] = file;
	r = binaries_complete(&file->deb, filekey, file->md5sum, oinfo,
			section, priority, &package->control);
	if( RET_WAS_ERROR(r) )
		return r;
	return RET_OK;
}

static retvalue prepare_dsc(filesdb filesdb,const struct incoming *i,const struct candidate *c,struct candidate_perdistribution *per,const struct candidate_file *file) {
	const char *section,*priority;
	const struct overrideinfo *oinfo;
	struct candidate_package *package;
	const struct distribution *into = per->into;
	retvalue r;
	int j;

	assert( file->type == fe_DSC );
	assert( file->tempfilename != NULL );
	assert( file->dsc.name != NULL );

	package = candidate_newpackage(per, file);
	if( package == NULL )
		return RET_ERROR_OOM;
	assert( file == package->master );

	if( strcmp(file->name, file->dsc.name) != 0 ) {
		// TODO: add permissive thing to ignore this
		fprintf(stderr, "Name part of filename ('%s') and name within the file ('%s') do not match for '%s' in '%s'!\n",
				file->name, file->dsc.name,
				BASENAME(i,file->ofs), BASENAME(i,c->ofs));
		return RET_ERROR;
	}
	if( strcmp(c->source, file->dsc.name) != 0 ) {
		// TODO: add permissive thing to ignore this
		// (beware if tracking is active)
		fprintf(stderr, "Source-header '%s' of '%s' and name '%s' within the file '%s' do not match!\n",
				c->source, BASENAME(i,c->ofs),
				file->dsc.name, BASENAME(i,file->ofs));
		return RET_ERROR;
	}
	if( strcmp(c->version, file->dsc.version) != 0 ) {
		// TODO: add permissive thing to ignore this
		// (beware if tracking is active)
		fprintf(stderr, "Version-header '%s' of '%s' and version '%s' within the file '%s' do not match!\n",
				c->version, BASENAME(i,c->ofs),
				file->dsc.version, BASENAME(i,file->ofs));
		return RET_ERROR;
	}
	r = propersourcename(file->dsc.name);
	if( RET_IS_OK(r) )
		r = properversion(file->dsc.version);
	if( RET_IS_OK(r) )
		r = properfilenames(&file->dsc.basenames);
	if( RET_WAS_ERROR(r) )
		return r;
	oinfo = override_search(into->overrides.dsc, file->dsc.name);

	r = getsectionprioritycomponent(i, c, into, file,
			file->dsc.name, oinfo,
			&section, &priority, &package->component);
	if( RET_WAS_ERROR(r) )
		return r;
	package->directory = calc_sourcedir(package->component, file->dsc.name);
	if( package->directory == NULL )
		return RET_ERROR_OOM;
	r = calc_dirconcats(package->directory, &file->dsc.basenames, &package->filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	package->files = calloc(package->filekeys.count,sizeof(struct candidate *));
	if( package->files == NULL )
		return RET_ERROR_OOM;
	r = files_ready(filesdb, package->filekeys.values[0], file->md5sum);
	if( RET_IS_OK(r) )
		package->files[0] = file;
	if( RET_WAS_ERROR(r) )
		return r;
	for( j = 1 ; j < package->filekeys.count ; j++ ) {
		const char *filekey = package->filekeys.values[j];
		const char *basename = file->dsc.basenames.values[j];
		const char *md5sum = file->dsc.md5sums.values[j];
		struct candidate_file *f = c->files;

		while( f != NULL && (f->md5sum == NULL ||
				     strcmp(BASENAME(i,f->ofs), basename) != 0) )
			f = f->next;

		if( f != NULL && strcmp(f->md5sum,md5sum) != 0 ) {
			fprintf(stderr, "file '%s' is listed with md5sum '%s' in '%s' but with md5sum '%s' in '%s'!\n",
					basename,
					f->md5sum, BASENAME(i,c->ofs),
					md5sum, BASENAME(i,file->ofs));
			return RET_ERROR;
		}
		r = files_ready(filesdb, filekey, md5sum);
		if( r == RET_NOTHING ) {
			/* already in the pool, mark as used (in the sense
			 * of "only not needed because it is already there") */

			if( f != NULL )
				f->used = TRUE;

		} else if( RET_IS_OK(r) ) {
			/* don't have this file in the pool, make sure it is ready
			 * here */

			if( f == NULL ) {
				/* if md5sum and size match, it's our file */
				f = c->files;
				while( f != NULL && ( f->md5sum == NULL
				                 || strcmp(f->md5sum,md5sum)) != 0 )
					f = f->next;
			}

			if( f == NULL ) {
				fprintf(stderr, "file '%s' is needed for '%s', not yet registered in the pool and not found in '%s'\n",
						basename, BASENAME(i,file->ofs),
						BASENAME(i,c->ofs));
				return RET_ERROR;
			}

			r = candidate_usefile(i,c, f);
			if( RET_WAS_ERROR(r) )
				return r;
			package->files[j] = f;
		}
		if( RET_WAS_ERROR(r) )
			return r;
	}
	r = sources_complete(&file->dsc, package->directory, oinfo, section, priority, &package->control);
	if( RET_WAS_ERROR(r) )
		return r;

	return RET_OK;
}

static retvalue prepare_for_distribution(filesdb filesdb,const struct incoming *i,const struct candidate *c,struct candidate_perdistribution *d) {
	struct candidate_file *file;
	retvalue r;

	d->into->lookedat = TRUE;

	for( file = c->files ; file != NULL ; file = file->next ) {
		switch( file->type ) {
			case fe_UDEB:
			case fe_DEB:
				r = prepare_deb(filesdb,i,c,d,file);
				break;
			case fe_DSC:
				r = prepare_dsc(filesdb,i,c,d,file);
				break;
			default:
				r = RET_NOTHING;
				break;
		}
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
	}
	if( d->into->tracking != dt_NONE ) {
		if( d->into->trackingoptions.includechanges ) {
			r = candidate_preparechangesfile(filesdb, i, c, d);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return RET_OK;
}

static retvalue candidate_removefiles(filesdb filesdb,struct candidate *c,struct candidate_perdistribution *stopat,struct candidate_package *stopatatstopat,int stopatatstopatatstopat) {
	int j;
	struct candidate_perdistribution *d;
	struct candidate_package *p;
	retvalue r;

	for( d = c->perdistribution ; d != NULL ; d = d->next ) {
		for( p = d->packages ; p != NULL ; p = p->next ) {
			for( j = 0 ; j < p->filekeys.count ; j++ ) {
				if( d == stopat &&
				    p == stopatatstopat &&
				    j >= stopatatstopatatstopat )
					return RET_OK;

				if(  p->files[j] == NULL )
					continue;
				r = files_deleteandremove(filesdb,
						p->filekeys.values[j],
						TRUE, TRUE);
				if( RET_WAS_ERROR(r) )
					return r;
			}
		}
	}
	return RET_OK;
}

static retvalue candidate_addfiles(filesdb filesdb,struct incoming *i,struct candidate *c) {
	int j;
	struct candidate_perdistribution *d;
	struct candidate_package *p;
	retvalue r;

	for( d = c->perdistribution ; d != NULL ; d = d->next ) {
		for( p = d->packages ; p != NULL ; p = p->next ) {
			for( j = 0 ; j < p->filekeys.count ; j++ ) {
				const struct candidate_file *f = p->files[j];
				if(  f == NULL )
					continue;
				assert(f->tempfilename != NULL);
				r = files_hardlink(filesdb, f->tempfilename,
						p->filekeys.values[j],
						f->md5sum);
				if( !RET_IS_OK(r) )
					/* when we did not add it, do not remove it: */
					p->files[j] = NULL;
				if( RET_WAS_ERROR(r) ) {
					candidate_removefiles(filesdb, c, d, p, j);
					return r;
				}
			}
		}
	}
	return RET_OK;
}

static retvalue add_dsc(const char *dbdir, references refs,
		struct distribution *into, struct strlist *dereferenced,
		const struct incoming *i, const struct candidate *c,
		struct trackingdata *trackingdata, struct candidate_package *p) {
	retvalue r;
	struct target *t = distribution_getpart(into, p->component, "source", "dsc");

	assert( logger_isprepared(into->logger) );

	/* finally put it into the source distribution */
	r = target_initpackagesdb(t,dbdir);
	if( !RET_WAS_ERROR(r) ) {
		retvalue r2;
		if( interrupted() )
			r = RET_ERROR_INTERUPTED;
		else
			r = target_addpackage(t, into->logger, refs,
					p->master->dsc.name,
					p->master->dsc.version,
					p->control,
					&p->filekeys,
					FALSE, dereferenced,
					trackingdata, ft_SOURCE);
		r2 = target_closepackagesdb(t);
		RET_ENDUPDATE(r,r2);
	}
	RET_UPDATE(into->status, r);
	return r;
}

static retvalue candidate_add_into(const char *confdir,filesdb filesdb,const char *dbdir,references refs,struct strlist *dereferenced,const struct incoming *i,const struct candidate *c,const struct candidate_perdistribution *d) {
	retvalue r;
	struct candidate_package *p;
	struct trackingdata trackingdata;
	struct distribution *into = d->into;
	trackingdb tracks;

	if( interrupted() )
		return RET_ERROR_INTERUPTED;

	d->into->lookedat = TRUE;
	if( d->into->logger != NULL ) {
		r = logger_prepare(d->into->logger);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	tracks = NULL;
	if( into->tracking != dt_NONE ) {
		r = tracking_initialize(&tracks, dbdir, into);
		if( RET_WAS_ERROR(r) )
			return r;
	}
	if( tracks != NULL ) {
		r = trackingdata_summon(tracks, c->source, c->version, &trackingdata);
		if( RET_WAS_ERROR(r) ) {
			tracking_done(tracks);
			return r;
		}
		if( into->trackingoptions.needsources ) {
			// TODO, but better before we start adding...
		}
	}

	r = RET_OK;
	for( p = d->packages ; p != NULL ; p = p->next ) {
		if( p->master->type == fe_DSC ) {
			r = add_dsc(dbdir, refs, into, dereferenced,
					i, c, (tracks==NULL)?NULL:&trackingdata,
					p);
		} else if( FE_BINARY(p->master->type) ) {
			r = binaries_adddeb(&p->master->deb, dbdir, refs,
					p->master->architecture,
					(p->master->type == fe_DEB)?"deb":"udeb",
					into, dereferenced,
					(tracks==NULL)?NULL:&trackingdata,
					p->component, &p->filekeys, p->control);
		} else if( p->master->type == fe_UNKNOWN ) {
			/* finally add the .changes to tracking, if requested */
			assert( p->master->name == NULL );
			assert( tracks != NULL );

			r = trackedpackage_adddupfilekeys(trackingdata.tracks,
					trackingdata.pkg,
					ft_CHANGES, &p->filekeys, FALSE, refs);
		} else
			r = RET_ERROR;

		if( RET_WAS_ERROR(r) ) {
			// TODO: remove files not yet referenced
			break;
		}
	}

	if( tracks != NULL ) {
		retvalue r2;
		r2 = trackingdata_finish(tracks, &trackingdata,
				refs, dereferenced);
		RET_UPDATE(r,r2);
		r2 = tracking_done(tracks);
		RET_ENDUPDATE(r,r2);
	}
	if( RET_WAS_ERROR(r) )
		return r;
	return RET_OK;
}

static inline bool_t isallowed(struct incoming *i, struct candidate *c, struct distribution *into, const struct uploadpermissions *permissions) {
	return permissions->allowall;
}

static retvalue candidate_checkpermissions(const char *confdir, struct incoming *i, struct candidate *c, struct distribution *into) {
	retvalue r;
	int j;

	/* no rules means allowed */
	if( into->uploaders == NULL )
		return RET_OK;

	r = distribution_loaduploaders(into, confdir);
	if( RET_WAS_ERROR(r) )
		return r;
	assert(into->uploaderslist != NULL);

	if( c->keys.count == 0 ) {
		const struct uploadpermissions *permissions;

		r = uploaders_unsignedpermissions(into->uploaderslist,
				&permissions);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
		if( permissions != NULL && isallowed(i,c,into,permissions) )
			return RET_OK;
	} else for( j = 0; j < c->keys.count ; j++ ) {
		const struct uploadpermissions *permissions;

		r = uploaders_permissions(into->uploaderslist,
				c->keys.values[j], &permissions);
		assert( r != RET_NOTHING );
		if( RET_WAS_ERROR(r) ) {
			return r;
		}
		if( permissions != NULL && isallowed(i,c,into,permissions) )
			return RET_OK;
	}
	/* reject */
	return RET_NOTHING;
}

static retvalue check_architecture_availability(const struct incoming *i, const struct candidate *c) {
	struct candidate_perdistribution *d;
	bool_t check_all_availability = FALSE;
	bool_t have_all_available = FALSE;
	int j;

	// TODO: switch to instead ensure every architecture can be put into
	// one distribution at least would be nice. If implementing this do not
	// forget to check later to only put files in when the distribution can
	// cope with that.

	for( j = 0 ; j < c->architectures.count ; j++ ) {
		const char *architecture = c->architectures.values[j];
		if( strcmp(architecture, "all") == 0 ) {
			check_all_availability = TRUE;
			continue;
		}
		for( d = c->perdistribution ; d != NULL ; d = d->next ) {
			if( strlist_in(&d->into->architectures, architecture) )
				continue;
			fprintf(stderr, "'%s' lists architecture '%s' not found in distribution '%s'!\n",
					BASENAME(i,c->ofs), architecture, d->into->codename);
			return RET_ERROR;
		}
		if( strcmp(architecture, "source") != 0 )
			have_all_available = TRUE;
	}
	if( check_all_availability && ! have_all_available ) {
		for( d = c->perdistribution ; d != NULL ; d = d->next ) {
			if( d->into->architectures.count > 1 )
				continue;
			if( d->into->architectures.count > 0 &&
				strcmp(d->into->architectures.values[0],"source") != 0)
				continue;
			fprintf(stderr, "'%s' lists architecture 'all' but not binary architecture found in distribution '%s'!\n",
					BASENAME(i,c->ofs), d->into->codename);
			return RET_ERROR;
		}
	}
	return RET_OK;
}

static retvalue candidate_add(const char *confdir,const char *overridedir,filesdb filesdb, const char *dbdir, references refs, struct strlist *dereferenced, struct incoming *i, struct candidate *c) {
	struct candidate_perdistribution *d;
	struct candidate_file *file;
	retvalue r;
	assert( c->perdistribution != NULL );

	/* check if every distribution this is to be added to supports
	 * all architectures we have files for */
	r = check_architecture_availability(i, c);
	if( RET_WAS_ERROR(r) )
		return r;

	for( d = c->perdistribution ; d != NULL ; d = d->next ) {
		r = distribution_loadalloverrides(d->into, overridedir);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	// TODO: once uploaderlist allows to look for package names or existing override
	// entries or such things, check package names here enable checking for content
	// name with outer name

	/* when we get here, the package is allowed in, now we have to
	 * read the parts and check all stuff we only know now */

	r = candidate_read_files(i, c);
	if( RET_WAS_ERROR(r) )
		return r;

	/* now the distribution specific part starts: */
	for( d = c->perdistribution ; d != NULL ; d = d->next ) {
		r = prepare_for_distribution(filesdb, i, c, d);
			if( RET_WAS_ERROR(r) ) {
				return r;
			}
	}
	for( file = c->files ; file != NULL ; file = file->next ) {
		if( !file->used && !i->permit.unused_files ) {
			// TODO: other error function
			fprintf(stderr,
"Error: '%s' contains unused file '%s'!\n",
				BASENAME(i,c->ofs), BASENAME(i,file->ofs));
			return RET_ERROR;

		}
	}

	// TODO: make sure not two different files are supposed to be installed
	// as the same filekey.

	/* the actual adding of packages, make sure what can be checked was
	 * checked by now */

	/* make hardlinks/copies of the files */
	r = candidate_addfiles(filesdb, i, c);
	if( RET_WAS_ERROR(r) )
		return r;
	if( interrupted() ) {
		candidate_removefiles(filesdb,c,NULL,NULL,0);
		return RET_ERROR_INTERUPTED;
	}
	r = RET_OK;
	for( d = c->perdistribution ; d != NULL ; d = d->next ) {
		r = candidate_add_into(confdir, filesdb, dbdir, refs,
			dereferenced, i, c, d);
		if( RET_WAS_ERROR(r) )
			return r;
	}

	/* mark files as done */
	for( file = c->files ; file != NULL ; file = file->next ) {
		if( file->used || i->cleanup.unused_files ) {
			i->delete[file->ofs] = TRUE;
		}
	}
	return RET_OK;
}

static retvalue process_changes(const char *confdir,const char *overridedir,filesdb filesdb,const char *dbdir,references refs,struct strlist *dereferenced,struct incoming *i,int ofs) {
	struct candidate *c;
	struct candidate_file *file;
	retvalue r;
	int j,k;
	bool_t broken = FALSE, tried = FALSE;

	r = candidate_read(i, ofs, &c, &broken);
	if( RET_WAS_ERROR(r) )
		return r;
	assert( RET_IS_OK(r) );
	r = candidate_parse(i, c);
	if( RET_WAS_ERROR(r) ) {
		candidate_free(c);
		return r;
	}
	r = candidate_earlychecks(i, c);
	if( RET_WAS_ERROR(r) ) {
		candidate_free(c);
		return r;
	}
	for( k = 0 ; k < c->distributions.count ; k++ ) {
		const char *name = c->distributions.values[k];

		for( j = 0 ; j < i->allow.count ; j++ ) {
			// TODO: implement "*"
			if( strcmp(name, i->allow.values[j]) == 0 ) {
				tried = TRUE;
				r = candidate_checkpermissions(confdir, i, c,
						i->allow_into[j]);
				if( r == RET_NOTHING )
					continue;
				if( RET_IS_OK(r) )
					r = candidate_newdistribution(c,
							i->allow_into[j]);
				if( RET_WAS_ERROR(r) ) {
					candidate_free(c);
					return r;
				} else
					break;
			}
		}
		if( c->perdistribution != NULL &&
				!i->permit.multiple_distributions )
			break;
	}
	if( c->perdistribution == NULL && i->default_into != NULL ) {
		tried = TRUE;
		r = candidate_checkpermissions(confdir, i, c, i->default_into);
		if( RET_WAS_ERROR(r) ) {
			candidate_free(c);
			return r;
		}
		if( RET_IS_OK(r) ) {
			r = candidate_newdistribution(c, i->default_into);
		}
	}
	if( c->perdistribution == NULL ) {
		fprintf(stderr, tried?"No distribution accepting '%s'!\n":
				      "No distribution found for '%s'!\n",
			i->files.values[ofs]);
		if( i->cleanup.on_deny  ) {
			i->delete[c->ofs] = TRUE;
			for( file = c->files ; file != NULL ; file = file->next ) {
				// TODO: implement same-owner check
				if( !i->cleanup.on_deny_check_owner )
					i->delete[file->ofs] = TRUE;
			}
		}
		r = RET_ERROR;
	} else {
		if( broken ) {
			fprintf(stderr,
"'%s' is signed with only invalid signatures.\n"
"If this was not corruption but willfull modification,\n"
"remove the signatures and try again.\n",
				i->files.values[ofs]);
			r = RET_ERROR;
		} else
			r = candidate_add(confdir, overridedir, filesdb, dbdir,
			                  refs, dereferenced,
			                  i, c);
		if( RET_WAS_ERROR(r) && i->cleanup.on_error ) {
			struct candidate_file *file;

			i->delete[c->ofs] = TRUE;
			for( file = c->files ; file != NULL ; file = file->next ) {
				i->delete[file->ofs] = TRUE;
			}
		}
	}
	candidate_free(c);
	return r;
}

/* tempdir should ideally be on the same partition like the pooldir */
retvalue process_incoming(const char *basedir,const char *confdir,const char *overridedir,filesdb files,const char *dbdir,references refs,struct strlist *dereferenced,struct distribution *distributions,const char *name) {
	struct incoming *i;
	retvalue result,r;
	int j;

	result = RET_NOTHING;

	r = incoming_init(basedir, confdir, distributions, name, &i);
	if( RET_WAS_ERROR(r) )
		return r;

	for( j = 0 ; j < i->files.count ; j ++ ) {
		const char *basename = i->files.values[j];
		size_t l = strlen(basename);
#define C_SUFFIX ".changes"
#define C_LEN strlen(C_SUFFIX)
		if( l <= C_LEN || strcmp(basename+(l-C_LEN),C_SUFFIX) != 0 )
			continue;
		/* a .changes file, check it */
		r = process_changes(confdir, overridedir, files, dbdir, refs, dereferenced, i, j);
		RET_UPDATE(result, r);
	}

	for( j = 0 ; j < i->files.count ; j ++ ) {
		char *fullfilename;

		if( !i->delete[j] )
			continue;

		fullfilename = calc_dirconcat(i->directory, i->files.values[j]);
		if( fullfilename == NULL ) {
			result = RET_ERROR_OOM;
			continue;
		}
		if( verbose >= 3 )
			printf("deleting '%s'...\n", fullfilename);
		copyfile_delete(fullfilename);
		free(fullfilename);
	}
	incoming_free(i);
	logger_wait();
	return result;
}
