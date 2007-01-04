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
	const char *name;
	struct distribution *distributions;
	struct incoming *i;
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
		"IncomingDir", "Default", "Allow",
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
	r = chunk_getvalue(chunk, "IncomingDir", &i->directory);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"Expected 'IncomingDir' header not found in definition for '%s' in '%s'!\n", d->name, d->filename);
		r = RET_ERROR_MISSING;
	}
	if( RET_WAS_ERROR(r) ) {
		incoming_free(i);
		return r;
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
			fprintf(stderr, "'%s' in '%s' has neither a 'Allow' nor a 'Default' definition!\nAborting as nothing would be left in.\n", d->name, d->filename);
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
	d->i = i;
	return RET_OK;
}

static retvalue incoming_init(const char *confdir, struct distribution *distributions, const char *name, struct incoming **result) {
	retvalue r;
	struct importsparsedata imports;

	imports.name = name;
	imports.distributions = distributions;
	imports.i = NULL;
	imports.filename = calc_dirconcat(confdir, "imports");
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
		enum filetype type;
		char *md5sum;
		char *section;
		char *priority;
		char *architecture;
		char *name;
		/* set later */
		bool_t used;
		char *tempfilename;
		/* only for deb and dsc: */
		char *component;
		struct deb_headers deb;
		struct dsc_headers dsc;
		char *directory;
		struct strlist filekeys;
		/* a list of pointers to the files belonging to those
		 * filekeys, NULL if it does not need linking/copying */
		struct candidate_file **files;
		/* ... */
	} *files;
};

static void candidate_free(struct candidate *c) {
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
	while( c->files != NULL ) {
		struct candidate_file *f = c->files;
		c->files = f->next;
		free(f->md5sum);
		free(f->section);
		free(f->priority);
		free(f->architecture);
		free(f->name);
		if( f->tempfilename != NULL ) {
			unlink(f->tempfilename);
			free(f->tempfilename);
			f->tempfilename = NULL;
		}
		free(f->component);
		if( FE_BINARY(f->type) )
			binaries_debdone(&f->deb);
		if( f->type == fe_DSC )
			sources_done(&f->dsc);
		free(f->directory);
		strlist_done(&f->filekeys);
		free(f->files);
		free(f);
	}
	free(c);
}

static retvalue candidate_read(struct incoming *i, int ofs, struct candidate **result, bool_t *broken) {
	struct candidate *n;
	retvalue r;

	n = calloc(1,sizeof(struct candidate));
	if( n == NULL )
		return RET_ERROR_OOM;
	n->ofs = ofs;
	n->fullfilename = calc_dirconcat(i->directory, i->files.values[ofs]);
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
			fprintf(stderr,"In '%s': " err "\n",i->files.values[c->ofs]); \
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
	E("Missing 'Architecture' field1");
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
	if( c->files == NULL ) {
		fprintf(stderr,"In '%s': Empty 'Files' section!\n",i->files.values[c->ofs]);
		return RET_ERROR;
	}
	return RET_OK;
}
static retvalue candidate_earlychecks(struct incoming *i, struct candidate *c) {
	return RET_OK;
}
static retvalue candidate_usefile(struct incoming *i,struct candidate *c,struct candidate_file *file) {
	const char *basename;
	char *origfile,*tempfilename;
	retvalue r;

	if( file->used ) {
		assert(file->tempfilename != NULL);
		return RET_OK;
	}
	basename = BASENAME(i,file->ofs);
	tempfilename = calc_dirconcat(i->tempdir, basename);
	if( tempfilename == NULL )
		return RET_ERROR_OOM;
	origfile = calc_dirconcat(i->directory, basename);
	if( origfile == NULL ) {
		free(tempfilename);
		return RET_ERROR_OOM;
	}
	unlink(tempfilename);
	r = copy(tempfilename, origfile, file->md5sum, NULL);
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

static inline retvalue getsectionprioritycomponent(struct incoming *i,struct candidate *c,struct distribution *into,struct candidate_file *file, const char *name, const struct overrideinfo *oinfo, const char **section_p, const char **priority_p) {
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
				BASENAME(i,c->ofs), BASENAME(i,file->ofs));
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
				BASENAME(i,c->ofs), BASENAME(i,file->ofs));
		return RET_ERROR;
	}

	r = guess_component(into->codename,&into->components,BASENAME(i,file->ofs),section,NULL,&file->component);
	if( RET_WAS_ERROR(r) ) {
		return r;
	}
	*section_p = section;
	*priority_p = priority;
	return RET_OK;
}

static retvalue prepare_deb(filesdb filesdb, struct incoming *i,struct candidate *c,struct distribution *into,struct candidate_file *file) {
	const char *section,*priority, *filekey;
	const struct overrideinfo *oinfo;
	retvalue r;

	assert( FE_BINARY(file->type) );

	r = candidate_usefile(i, c, file);
	if( RET_WAS_ERROR(r) )
		return r;
	assert(file->tempfilename != NULL);
	r = binaries_readdeb(&file->deb, file->tempfilename, TRUE);
	if( RET_WAS_ERROR(r) )
		return r;
	if( strcmp(file->name, file->deb.name) != 0 ) {
		// TODO: add permissive thing to ignore this
		fprintf(stderr, "Name part of filename ('%s') and name within the file ('%s') do not match for '%s' in '%s'!\n",
				file->name, file->deb.name,
				BASENAME(i,c->ofs), BASENAME(i,file->ofs));
		return RET_ERROR;
	}
	if( strcmp(file->architecture, file->deb.architecture) != 0 ) {
		// TODO: add permissive thing to ignore this in some cases
		fprintf(stderr, "Architecture '%s' of '%s' does not match '%s' specified in '%s'!\n",
				file->deb.architecture, BASENAME(i,c->ofs),
				file->architecture, BASENAME(i,file->ofs));
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
		fprintf(stderr, "Name '%s' of binary '%s' is not listed in Binaries-header of'%s'!\n",
				c->version, BASENAME(i,c->ofs),
				BASENAME(i,file->ofs));
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

	oinfo = override_search(file->type==fe_UDEB?into->overrides.udeb:
			                    into->overrides.deb,
	                        file->name);

	r = getsectionprioritycomponent(i,c,into,file,
			file->name, oinfo,
			&section, &priority);
	if( RET_WAS_ERROR(r) )
		return r;

	if( file->type == fe_UDEB &&
	    !strlist_in(&into->udebcomponents,file->component)) {
		fprintf(stderr,
"Cannot put file '%s' of '%s' into component '%s',\n"
"as it is not listed in UDebComponents of '%s'!\n",
			BASENAME(i,file->ofs), BASENAME(i,c->ofs),
			file->component, into->codename);
		return RET_ERROR;
	}
	r = binaries_calcfilekeys(file->component, &file->deb,
			(file->type==fe_DEB)?"deb":"udeb", &file->filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	assert( file->filekeys.count == 1 );
	filekey = file->filekeys.values[0];
	file->files = calloc(1, sizeof(struct candidate_file *));
	if( file->files == NULL )
		return RET_ERROR_OOM;
	r = files_ready(filesdb, filekey, file->md5sum);
	if( RET_WAS_ERROR(r) )
		return r;
	if( RET_IS_OK(r) )
		file->files[0] = file;
	r = binaries_complete(&file->deb, filekey, file->md5sum, oinfo,
			section, priority);
	if( RET_WAS_ERROR(r) )
		return r;
	return RET_OK;
}

static retvalue prepare_dsc(filesdb filesdb, struct incoming *i,struct candidate *c,struct distribution *into,struct candidate_file *file) {
	const char *section,*priority;
	const struct overrideinfo *oinfo;
	retvalue r;
	char *p;
	bool_t broken;
	int j;

	assert( file->type == fe_DSC );

	r = candidate_usefile(i,c,file);
	if( RET_WAS_ERROR(r) )
		return r;
	assert(file->tempfilename != NULL);
	r = sources_readdsc(&file->dsc, file->tempfilename, &broken);
	if( RET_WAS_ERROR(r) )
		return r;
	if( strcmp(file->name, file->dsc.name) != 0 ) {
		// TODO: add permissive thing to ignore this
		fprintf(stderr, "Name part of filename ('%s') and name within the file ('%s') do not match for '%s' in '%s'!\n",
				file->name, file->dsc.name,
				BASENAME(i,c->ofs), BASENAME(i,file->ofs));
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
	if( RET_IS_OK(r) )
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
			&section, &priority);
	if( RET_WAS_ERROR(r) )
		return r;
	// or should we keep that filename, too?
	// TODO: check what dpkg-source uses, the fields from .dsc or the name
	p = calc_source_basename(file->dsc.name, file->dsc.version);
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
	file->directory = calc_sourcedir(file->component, file->dsc.name);
	if( file->directory == NULL )
		return RET_ERROR_OOM;
	r = calc_dirconcats(file->directory, &file->dsc.basenames, &file->filekeys);
	if( RET_WAS_ERROR(r) )
		return r;
	file->files = calloc(file->filekeys.count,sizeof(struct candidate *));
	if( file->files == NULL )
		return RET_ERROR_OOM;
	r = files_ready(filesdb, file->filekeys.values[0], file->md5sum);
	if( RET_IS_OK(r) )
		file->files[0] = file;
	if( RET_WAS_ERROR(r) )
		return r;
	file->files[0] = file;
	for( j = 1 ; j < file->filekeys.count ; j++ ) {
		const char *filekey = file->filekeys.values[j];
		const char *basename = file->dsc.basenames.values[j];
		const char *md5sum = file->dsc.md5sums.values[j];
		struct candidate_file *f = c->files;

		while( f != NULL && strcmp(BASENAME(i,f->ofs), basename) != 0 )
			f = f->next;

		if( f != NULL && strcmp(f->md5sum,md5sum) != 0 ) {
			fprintf(stderr, "file '%s' is listed with md5sum '%s' in '%s' but with md5sum '%s' in '%s'!\n",
					basename,
					file->md5sum, BASENAME(i,c->ofs),
					md5sum, BASENAME(i,file->ofs));

		}
		r = files_ready(filesdb, filekey, md5sum);
		if( r == RET_NOTHING ) {
			/* already in the pool, make as used (in the sense
			 * of not needed) */

			if( f != NULL )
				f->used = TRUE;

		} else if( RET_IS_OK(r) ) {
			/* don't have this file in the pool, make sure it is ready
			 * here */

			if( f == NULL ) {
				fprintf(stderr, "file '%s' is needed for '%s', not yet registered in the pool and not found in '%s'\n",
						basename, BASENAME(i,file->ofs),
						BASENAME(i,c->ofs));
				return RET_ERROR;
			}

			r = candidate_usefile(i,c, f);
			if( RET_WAS_ERROR(r) )
				return r;
			file->files[j] = f;
		}
		if( RET_WAS_ERROR(r) )
			return r;
	}
	r = sources_complete(&file->dsc, file->directory, oinfo, section, priority);
	if( RET_WAS_ERROR(r) )
		return r;

	return RET_OK;
}

static retvalue candidate_removefiles(filesdb filesdb,struct candidate *c,struct candidate_file *stopat,int stopatatstopat) {
	int j;
	struct candidate_file *file;
	retvalue r;

	for( file = c->files ; file != NULL ; file = (file==stopat)?NULL:file->next ) {
		if( ! FE_PACKAGE(file->type) )
			continue;

		for( j = 0 ;
		     j < file->filekeys.count
		     && ( file != stopat || j < stopatatstopat ) ;
		     j++ ) {
			if(  file->files[j] == NULL )
				continue;
			r = files_deleteandremove(filesdb,
					file->filekeys.values[j],
					TRUE, TRUE);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	return RET_OK;
}

static retvalue candidate_addfiles(filesdb filesdb,struct incoming *i,struct candidate *c) {
	int j;
	struct candidate_file *file;
	retvalue r;

	for( file = c->files ; file != NULL ; file = file->next ) {
		if( ! FE_PACKAGE(file->type) )
			continue;

		for( j = 0 ; j < file->filekeys.count ; j++ ) {
			struct candidate_file *f = file->files[j];
			if(  f == NULL )
				continue;
			assert(f->tempfilename != NULL);
			r = files_hardlink(filesdb, f->tempfilename,
					file->filekeys.values[j],
					f->md5sum);
			if( !RET_IS_OK(r) )
				/* when we did not add it, do not remove it: */
				file->files[j] = NULL;
			if( RET_WAS_ERROR(r) ) {
				candidate_removefiles(filesdb, c, file, j);
				return r;
			}
		}
	}
	return RET_OK;
}
static retvalue add_dsc(const char *dbdir, references refs,
		struct distribution *into, struct strlist *dereferenced,
		struct incoming *i, struct candidate *c,
		struct trackingdata *trackingdata, struct candidate_file *file) {
	retvalue r;
	struct target *t = distribution_getpart(into,file->component,"source","dsc");

	/* finally put it into the source distribution */
	r = target_initpackagesdb(t,dbdir);
	if( !RET_WAS_ERROR(r) ) {
		retvalue r2;
		if( interrupted() )
			r = RET_ERROR_INTERUPTED;
		else
			r = target_addpackage(t,refs,
					file->dsc.name,file->dsc.version,
					file->dsc.control, &file->filekeys,
					FALSE, dereferenced,
					trackingdata, ft_SOURCE);
		r2 = target_closepackagesdb(t);
		RET_ENDUPDATE(r,r2);
	}
	RET_UPDATE(into->status, r);
	return r;
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

static retvalue candidate_add(const char *confdir, filesdb filesdb, const char *dbdir, references refs, struct strlist *dereferenced, struct incoming *i, struct candidate *c, struct distribution *into) {
	struct candidate_file *file;
	struct trackingdata *trackingdata = NULL;
	retvalue r;
	assert( into != NULL );

	if( into->tracking != dt_NONE ) {
		fprintf(stderr, "Distributions with tracking not yet supported for import!\n");
		return RET_NOTHING;
	}

	// TODO: allow being more permissive
	r = propersourcename(c->source);
	if( RET_WAS_ERROR(r) )
		return r;
	r = properversion(c->version);
	if( RET_WAS_ERROR(r) )
		return r;

	r = distribution_loadalloverrides(into,confdir);
	if( RET_WAS_ERROR(r) )
		return r;

	// TODO: once uploaderlist allows to look for package names or existing override
	// entries or such things, check package names here enable checking for content
	// name with outer name

	/* when we get here, the package is allowed in, now we have to
	 * read the parts and check all stuff we only know now */

	for( file = c->files ; file != NULL ; file = file->next ) {
		if( strcmp(file->section, "byhand") == 0 ) {
			/* to avoid further tests for this file */
			file->type = fe_UNKNOWN;
			// TODO: add command to feed them into
			// increment refcount when used
			continue;
		}
		switch( file->type ) {
			case fe_UDEB:
			case fe_DEB:
				r = prepare_deb(filesdb,i,c,into,file);
				break;
			case fe_DSC:
				r = prepare_dsc(filesdb,i,c,into,file);
				break;
			default:
				r = RET_NOTHING;
				break;
		}
		if( RET_WAS_ERROR(r) ) {
			// TODO: error reporting?
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

	if( interrupted() )
		return RET_ERROR_INTERUPTED;

	/* the actual adding of packages, make sure what can be checked was
	 * checked by now */

	/* make hardlinks/copies of the files */
	r = candidate_addfiles(filesdb, i, c);
	if( RET_WAS_ERROR(r) )
		return r;
	if( interrupted() ) {
		candidate_removefiles(filesdb,c,NULL,0);
		return RET_ERROR_INTERUPTED;
	}
	/* first add sources */
	for( file = c->files ; file != NULL ; file = file->next ) {
		if( file->type == fe_DSC ) {
			r = add_dsc(dbdir, refs, into, dereferenced,
					i, c, trackingdata, file);
			if( RET_WAS_ERROR(r) ) {
				// hrmpf, how to remove the packages?
				// candidate_removefiles(filesdb, c);
				return r;
			}
		}
	}
	/* then binaries */
	for( file = c->files ; file != NULL ; file = file->next ) {
		if( file->type == fe_DEB || file->type == fe_UDEB ) {
			r = binaries_adddeb(&file->deb, dbdir, refs,
					file->architecture,
					(file->type == fe_DEB)?"deb":"udeb",
					into, dereferenced,
					trackingdata,
					file->component, &file->filekeys);
			if( RET_WAS_ERROR(r) ) {
				// hrmpf, how to remove the packages?
				// candidate_removefiles(filesdb, c);
				return r;
			}
		}
	}
	/* everything installed, fire up the byhands */
	for( file = c->files ; file != NULL ; file = file->next ) {
		if( file->type == fe_UNKNOWN &&
		    strcmp(file->section, "byhand") == 0 ) {
			// TODO: add command to actualy use them
			continue;
		}
	}

	/* mark files as done */
	i->delete[c->ofs] = TRUE;
	for( file = c->files ; file != NULL ; file = file->next ) {
		if( file->used || i->cleanup.unused_files ) {
			i->delete[file->ofs] = TRUE;
		}
	}
	return RET_OK;
}

static retvalue process_changes(const char *confdir, filesdb filesdb, const char *dbdir, references refs, struct strlist *dereferenced, struct incoming *i, int ofs) {
	struct candidate *c;
	struct distribution *into = NULL;
	struct candidate_file *file;
	retvalue r;
	int j;
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
	for( j = 0 ; j < i->allow.count ; j++ ) {
		if( strlist_in(&c->distributions, i->allow.values[j]) ) {
			tried = TRUE;
			r = candidate_checkpermissions(confdir, i, c, i->allow_into[j]);
			if( RET_WAS_ERROR(r) ) {
				candidate_free(c);
				return r;
			}
			if( RET_IS_OK(r) ) {
				into = i->allow_into[j];
				break;
			}
		}
	}
	if( into == NULL && i->default_into != NULL ) {
		tried = TRUE;
		r = candidate_checkpermissions(confdir, i, c, i->default_into);
		if( RET_WAS_ERROR(r) ) {
			candidate_free(c);
			return r;
		}
		if( RET_IS_OK(r) ) {
			into = i->default_into;
		}
	}
	if( into == NULL ) {
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
			r = candidate_add(confdir, filesdb, dbdir,
			                  refs, dereferenced,
			                  i, c, into);
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
retvalue process_incoming(const char *confdir, filesdb files, const char *dbdir, references refs, struct strlist *dereferenced, struct distribution *distributions, const char *name) {
	struct incoming *i;
	retvalue result,r;
	int j;

	result = RET_NOTHING;

	r = incoming_init(confdir, distributions, name, &i);
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
		r = process_changes(confdir, files, dbdir, refs, dereferenced, i, j);
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
	return result;
}
