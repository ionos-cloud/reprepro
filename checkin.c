/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005,2006,2007 Bernhard R. Link
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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>
#include "error.h"
#include "ignore.h"
#include "strlist.h"
#include "md5sum.h"
#include "copyfile.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "reference.h"
#include "signature.h"
#include "sources.h"
#include "files.h"
#include "tracking.h"
#include "guesscomponent.h"
#include "override.h"
#include "checkindsc.h"
#include "checkindeb.h"
#include "checkin.h"
#include "uploaderslist.h"
#include "log.h"
#include "dpkgversions.h"
#include "changes.h"

extern int verbose;

/* Things to do when including a .changes-file:
 *  - Read in the chunk of the possible signed file.
 *    (In later versions possibly checking the signature)
 *  - Parse it, extracting:
 *  	+ Distribution
 * 	+ Source
 * 	+ Architecture
 * 	+ Binary
 * 	+ Version
 * 	+ ...
 * 	+ Files
 *  - Calculate what files are expectable...
 *  - Compare supplied filed with files expected.
 *  - (perhaps: write what was done and changes to some logfile)
 *  - add supplied files to the pool and register them in files.db
 *  - add the .dsc-files via checkindsc.c
 *  - add the .deb-filed via checkindeb.c
 *
 */

struct fileentry {
	struct fileentry *next;
	char *basename;
	filetype type;
	char *md5sum;
	char *section;
	char *priority;
	char *architecture;
	char *name;
	/* this might be different for different files,
	 * (though this is only allowed in rare cases),
	 * will be set by _fixfields */
	char *component;
	/* only set after changes_includefiles */
	char *filekey;
	/* was already found in the pool before */
	bool_t wasalreadythere;
	bool_t included;
	/* set between checkpkg and includepkg */
	union { struct dscpackage *dsc; struct debpackage *deb;} pkg;
};

struct changes {
	/* Things read by changes_read: */
	char *source, *sourceversion, *changesversion;
	struct strlist distributions,
		       architectures,
		       binaries;
	struct fileentry *files;
	char *control;
	struct strlist fingerprints;
	/* Things to be set by changes_fixfields: */
	/* the component source files are put into */
	const char *srccomponent;
	/* != NULL if changesfile was put into pool/ */
	/*@null@*/ char *changesfilekey;
	/* the directory where source files are put into */
	char *srcdirectory;
	/* (only to warn if multiple are used) */
	const char *firstcomponent;
	/* the directory the .changes file resides in */
	char *incomingdirectory;
	/* the Version: and the version in Source: differ */
	bool_t isbinnmu;
};

static void freeentries(/*@only@*/struct fileentry *entry) {
	struct fileentry *h;

	while( entry != NULL ) {
		h = entry->next;
		free(entry->filekey);
		free(entry->component);
		free(entry->basename);
		free(entry->md5sum);
		free(entry->section);
		free(entry->priority);
		free(entry->architecture);
		free(entry->name);
		if( entry->type == fe_DEB || entry->type == fe_UDEB)
			deb_free(entry->pkg.deb);
		 else if( entry->type == fe_DSC )
			dsc_free(entry->pkg.dsc);
		free(entry);
		entry = h;
	}
}

static void changes_free(/*@only@*/struct changes *changes) {
	if( changes != NULL ) {
		free(changes->source);
		free(changes->sourceversion);
		free(changes->changesversion);
		strlist_done(&changes->architectures);
		strlist_done(&changes->binaries);
		freeentries(changes->files);
		strlist_done(&changes->distributions);
		free(changes->control);
		free(changes->srcdirectory);
		free(changes->changesfilekey);
//		trackedpackage_free(changes->trackedpkg);
		free(changes->incomingdirectory);
		strlist_done(&changes->fingerprints);
	}
	free(changes);
}


static retvalue newentry(struct fileentry **entry,const char *fileline,const char *packagetypeonly,const char *forcearchitecture, const char *sourcename) {
	struct fileentry *e;
	retvalue r;

	e = calloc(1,sizeof(struct fileentry));
	if( e == NULL )
		return RET_ERROR_OOM;

	r = changes_parsefileline(fileline, &e->type, &e->basename, &e->md5sum,
			&e->section, &e->priority, &e->architecture, &e->name);
	if( RET_WAS_ERROR(r) ) {
		free(e);
		return r;
	}
	assert( RET_IS_OK(r) );
	if( FE_SOURCE(e->type) && packagetypeonly != NULL && strcmp(packagetypeonly,"dsc")!=0) {
		freeentries(e);
		return RET_NOTHING;
	}
	if( e->type == fe_DEB && packagetypeonly != NULL && strcmp(packagetypeonly,"deb")!=0) {
		freeentries(e);
		return RET_NOTHING;
	}
	if( e->type == fe_UDEB && packagetypeonly != NULL && strcmp(packagetypeonly,"udeb")!=0) {
		freeentries(e);
		return RET_NOTHING;
	}
	if( strcmp(e->architecture, "source") == 0 && strcmp(e->name, sourcename) != 0 ) {
		fprintf(stderr,"Warning: Strange file '%s'!\nLooks like source but does not start with '%s_' as I would have guessed!\nI hope you know what you do.\n",e->basename,sourcename);
	}

	if( forcearchitecture != NULL ) {
		if( strcmp(forcearchitecture,"source") != 0 &&
				strcmp(e->architecture,"all") == 0 ) {
			if( verbose > 2 )
				fprintf(stderr,"Placing '%s' only in architecture '%s' as requested.\n",e->basename,forcearchitecture);
			free(e->architecture);
			e->architecture = strdup(forcearchitecture);
		} else if( strcmp(forcearchitecture,e->architecture) != 0) {
			if( verbose > 1 )
				fprintf(stderr,"Skipping '%s' as not for architecture '%s'.\n",e->basename,forcearchitecture);
			freeentries(e);
			return RET_NOTHING;
		}
		if( e->architecture == NULL ) {
			freeentries(e);
			return RET_ERROR_OOM;
		}
	}

	e->next = *entry;
	*entry = e;
	return RET_OK;
}

/* Parse the Files-header to see what kind of files we carry around */
static retvalue changes_parsefilelines(const char *filename,struct changes *changes,const struct strlist *filelines,const char *packagetypeonly,const char *forcearchitecture) {
	retvalue result,r;
	int i;

	assert( changes->files == NULL);
	result = RET_NOTHING;

	for( i = 0 ; i < filelines->count ; i++ ) {
		const char *fileline = filelines->values[i];

		r = newentry(&changes->files,fileline,packagetypeonly,forcearchitecture,changes->source);
		RET_UPDATE(result,r);
		if( r == RET_ERROR )
			return r;
	}
	if( result == RET_NOTHING ) {
		fprintf(stderr,"%s: Not enough files in .changes!\n",filename);
		return RET_ERROR;
	}
	return result;
}

static retvalue check(const char *filename,struct changes *changes,const char *field) {
	retvalue r;

	r = chunk_checkfield(changes->control,field);
	if( r == RET_NOTHING ) {
		if( IGNORING("Ignoring","To Ignore",missingfield,
				"In '%s': Missing '%s' field!\n",filename,field) ) {
			return RET_OK;
		} else {
			return RET_ERROR;
		}
	}
	return r;
}

static retvalue changes_read(const char *filename,/*@out@*/struct changes **changes,/*@null@*/const char *packagetypeonly,/*@null@*/const char *forcearchitecture) {
	retvalue r;
	struct changes *c;
	struct strlist filelines;
	bool_t broken;
	int versioncmp;

#define E(err) { \
		if( r == RET_NOTHING ) { \
			fprintf(stderr,"In '%s': " err "\n",filename); \
			r = RET_ERROR; \
	  	} \
		if( RET_WAS_ERROR(r) ) { \
			changes_free(c); \
			return r; \
		} \
	}
#define R { \
		if( RET_WAS_ERROR(r) ) { \
			changes_free(c); \
			return r; \
		} \
	}


	c = calloc(1,sizeof(struct changes));
	if( c == NULL )
		return RET_ERROR_OOM;
	r = signature_readsignedchunk(filename, filename,
			&c->control, &c->fingerprints, NULL, &broken);
	R;
	if( broken && !IGNORING_(brokensignatures,
"'%s' contains only broken signatures.\n"
"This most likely means the file was damaged (or edited improperly)\n",
				filename) ) {
		r = RET_ERROR;
		R;
	}
	r = check(filename,c,"Format");
	R;
	r = check(filename,c,"Date");
	R;
	r = chunk_getnameandversion(c->control,"Source",&c->source,&c->sourceversion);
	E("Missing 'Source' field");
	r = propersourcename(c->source);
	R;
	if( c->sourceversion != NULL ) {
		r = properversion(c->sourceversion);
		R;
	}
	r = chunk_getwordlist(c->control,"Binary",&c->binaries);
	E("Missing 'Binary' field");
	r = chunk_getwordlist(c->control,"Architecture",&c->architectures);
	E("Missing 'Architecture' field");
	r = chunk_getvalue(c->control,"Version",&c->changesversion);
	E("Missing 'Version' field");
	r = properversion(c->changesversion);
	E("Malforce Version number");
	if( c->sourceversion == NULL ) {
		c->sourceversion = strdup(c->changesversion);
		if( c->sourceversion == NULL ) {
			changes_free(c);
			return RET_ERROR_OOM;
		}
		c->isbinnmu = FALSE;
	} else {
		r = dpkgversions_cmp(c->sourceversion, c->changesversion,
				&versioncmp);
		E("Error comparing versions. (That should have been caught earlier, why now?)");
		c->isbinnmu = versioncmp != 0;
	}
	r = chunk_getwordlist(c->control,"Distribution",&c->distributions);
	E("Missing 'Distribution' field");
	r = check(filename,c,"Urgency");
	R;
	r = check(filename,c,"Maintainer");
	R;
	r = check(filename,c,"Description");
	R;
	r = check(filename,c,"Changes");
	R;
	r = chunk_getextralinelist(c->control,"Files",&filelines);
	E("Missing 'Files' field!");
	r = changes_parsefilelines(filename,c,&filelines,packagetypeonly,forcearchitecture);
	strlist_done(&filelines);
	R;
	r = dirs_getdirectory(filename,&c->incomingdirectory);
	R;

	*changes = c;
	return RET_OK;
#undef E
#undef R
}

static retvalue changes_fixfields(const struct distribution *distribution,const char *filename,struct changes *changes,/*@null@*/const char *forcecomponent,/*@null@*/const char *forcesection,/*@null@*/const char *forcepriority) {
	struct fileentry *e;
	retvalue r;

	r = propersourcename(changes->source);
	if( RET_WAS_ERROR(r) )
		return r;

	e = changes->files;
	if( e == NULL ) {
		fprintf(stderr,"No files given in '%s'!\n",filename);
		return RET_ERROR;
	}

	while( e != NULL ) {
		const struct overrideinfo *oinfo = NULL;
		const char *force = NULL;
		if( forcesection == NULL || forcepriority == NULL ) {
			oinfo = override_search(
			FE_BINARY(e->type)?(e->type==fe_UDEB?distribution->overrides.udeb:distribution->overrides.deb):distribution->overrides.dsc,
					e->name);
		}

		if( forcesection != NULL )
			force = forcesection;
		else
			force = override_get(oinfo,SECTION_FIELDNAME);
		if( force != NULL ) {
			free(e->section);
			e->section = strdup(force);
			if( e->section == NULL )
				return RET_ERROR_OOM;
		}
		if( strcmp(e->section,"unknown") == 0 ) {
			fprintf(stderr,"Section '%s' of '%s' is not valid!\n",e->section,filename);
			return RET_ERROR;
		}
		if( strncmp(e->section,"byhand",6) == 0 ) {
			fprintf(stderr,"Cannot cope with 'byhand' file '%s'!\n",e->basename);
			return RET_ERROR;
		}
		if( strcmp(e->section,"-") == 0 ) {
			fprintf(stderr,"No section specified for '%s' in '%s'!\n", e->basename, filename);
			return RET_ERROR;
		}
		if( forcepriority != NULL )
			force = forcepriority;
		else
			force = override_get(oinfo,PRIORITY_FIELDNAME);
		if( force != NULL ) {
			free(e->priority);
			e->priority = strdup(force);
			if( e->priority == NULL )
				return RET_ERROR_OOM;
		}
		if( strcmp(e->priority,"-") == 0 ) {
			fprintf(stderr,"No priority specified for '%s'!\n",filename);
			return RET_ERROR;
		}

		// I'm undecided here. If this is a udeb, one could also use
		// distribution->udebcomponents. Though this might result
		// in not really predictable guesses for the section.
		r = guess_component(distribution->codename,&distribution->components,changes->source,e->section,forcecomponent,&e->component);
		if( RET_WAS_ERROR(r) )
			return r;
		assert(e->component != NULL);

		if( changes->firstcomponent == NULL ) {
			changes->firstcomponent = e->component;
		} else if( strcmp(changes->firstcomponent,e->component) != 0)  {
				fprintf(stderr,"Warning: %s contains files guessed to be in different components ('%s' vs '%s)!\nI hope you know what you do and this is not the cause of some broken override file.\n",filename,e->component,changes->firstcomponent);
		}

		if( FE_SOURCE(e->type) ) {
			if( strcmp(changes->source,e->name) != 0 ) {
				r = propersourcename(e->name);
				if( RET_WAS_ERROR(r) )
					return r;
			}
			if( changes->srccomponent == NULL ) {
				changes->srccomponent = e->component;
			} else if( strcmp(changes->srccomponent,e->component) != 0)  {
				fprintf(stderr,"%s contains source files guessed to be in different components ('%s' vs '%s)!\n",filename,e->component,changes->firstcomponent);
				return RET_ERROR;
			}
		} else if( FE_BINARY(e->type) ){
			r = properpackagename(e->name);
			if( RET_WAS_ERROR(r) )
				return r;
			r = properfilenamepart(e->architecture);
			if( RET_WAS_ERROR(r) )
				return r;
			// Let's just check here, perhaps
			if( e->type == fe_UDEB &&
					!strlist_in(&distribution->udebcomponents,e->component)) {
				fprintf(stderr,"Cannot put file '%s' into component '%s', as it is not listed in UDebComponents!\n",e->basename,e->component);
				return RET_ERROR;
			}
		} else {
			assert( FE_SOURCE(e->type) || FE_BINARY(e->type) );
			fprintf(stderr,"Internal Error!\n");
			return RET_ERROR;
		}

		e = e->next;
	}

	if( changes->srccomponent != NULL ) {
		changes->srcdirectory = calc_sourcedir(changes->srccomponent,changes->source);
		if( changes->srcdirectory == NULL )
			return RET_ERROR_OOM;
	} else if( distribution->trackingoptions.includechanges ) {
		const char *component = forcecomponent;
		if( forcecomponent == NULL ) {
			for( e = changes->files ; e != NULL ; e = e->next ) {
				if( FE_BINARY(e->type) ){
					component = e->component;
					break;
				}
			}
		}
		if( component == NULL ) {
			fprintf(stderr,"No component found to place .changes or byhand files in. Aborting.\n");
			return RET_ERROR;
		}
		changes->srcdirectory = calc_sourcedir(component,changes->source);
		if( changes->srcdirectory == NULL )
			return RET_ERROR_OOM;
	}

	return RET_OK;
}

static inline retvalue checkforarchitecture(const struct fileentry *e,const char *architecture ) {
	while( e !=NULL && strcmp(e->architecture,architecture) != 0 )
		e = e->next;
	if( e == NULL ) {
		if( !IGNORING_(unusedarch,"Architecture-header lists architecture '%s', but no files for this!\n",architecture))
			return RET_ERROR;
	}
	return RET_OK;
}

static retvalue changes_check(const char *filename,struct changes *changes,/*@null@*/const char *forcearchitecture, /*@null@*/const char *packagetypeonly) {
	int i;
	struct fileentry *e;
	retvalue r = RET_OK;
	bool_t havedsc=FALSE, haveorig=FALSE, havetar=FALSE, havediff=FALSE;

	/* First check for each given architecture, if it has files: */
	if( forcearchitecture != NULL ) {
		if( !strlist_in(&changes->architectures,forcearchitecture) ){
			// TODO: check if this is sensible
			if( !IGNORING_(surprisingarch,
				     "Architecture-header does not list the"
				     " architecture '%s' to be forced in!\n",
					forcearchitecture))
				return RET_ERROR_MISSING;
		}
		r = checkforarchitecture(changes->files,forcearchitecture);
		if( RET_WAS_ERROR(r) )
			return r;
	} else if( packagetypeonly == NULL ) {
		for( i = 0 ; i < changes->architectures.count ; i++ ) {
			const char *architecture = changes->architectures.values[i];
			r = checkforarchitecture(changes->files,
					architecture);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	} else if( strcmp(packagetypeonly,"dsc") == 0 ) {
		if( strlist_in(&changes->architectures,"source") ) {
			r = checkforarchitecture(changes->files, "source");
			if( RET_WAS_ERROR(r) )
				return r;
		}
	} else {
		for( i = 0 ; i < changes->architectures.count ; i++ ) {
			const char *architecture = changes->architectures.values[i];
			if( strcmp(architecture,"source") != 0 ) {
				r = checkforarchitecture(changes->files,
						architecture);
				if( RET_WAS_ERROR(r) )
					return r;
			}
		}
	}
	/* Then check for each file, if its architecture is sensible
	 * and listed. */
	e = changes->files;
	while( e != NULL ) {
		if( !strlist_in(&changes->architectures,e->architecture) ) {
			if( !IGNORING_(surprisingarch,
			"'%s' looks like architecture '%s', but this is not listed in the Architecture-Header!\n",e->basename,e->architecture))
				r = RET_ERROR;
		}
		if( e->type == fe_DSC ) {
			char *calculatedname;
			if( havedsc ) {
				fprintf(stderr,"I don't know what to do with multiple .dsc files in '%s'!\n",filename);
				return RET_ERROR;
			}
			havedsc = TRUE;
			calculatedname = calc_source_basename(changes->source,changes->sourceversion);
			if( calculatedname == NULL )
				return RET_ERROR_OOM;
			if( strcmp(calculatedname,e->basename) != 0 ) {
				fprintf(stderr,"dsc-filename is '%s' instead of the expected '%s'!\n",e->basename,calculatedname);
				free(calculatedname);
				return RET_ERROR;
			}
			free(calculatedname);
		} else if( e->type == fe_DIFF ) {
			if( havediff ) {
				fprintf(stderr,"I don't know what to do with multiple .diff files in '%s'!\n",filename);
				return RET_ERROR;
			}
			havediff = TRUE;
		} else if( e->type == fe_ORIG ) {
			if( haveorig ) {
				fprintf(stderr,"I don't know what to do with multiple .orig.tar.gz files in '%s'!\n",filename);
				return RET_ERROR;
			}
			haveorig = TRUE;
		} else if( e->type == fe_TAR ) {
			if( havetar ) {
				fprintf(stderr,"I don't know what to do with multiple .tar.gz files in '%s'!\n",filename);
				return RET_ERROR;
			}
			havetar = TRUE;
		}

		e = e->next;
	}

	if( havetar && haveorig ) {
		fprintf(stderr,"I don't know what to do having a .tar.gz and a .orig.tar.gz in '%s'!\n",filename);
		return RET_ERROR;
	}
	if( havetar && havediff ) {
		fprintf(stderr,"I don't know what to do having a .tar.gz not being a .orig.tar.gz and a .diff.gz in '%s'!\n",filename);
		return RET_ERROR;
	}
	if( strlist_in(&changes->architectures,"source") && !havedsc &&
			( forcearchitecture == NULL
			  || strcmp(forcearchitecture,"source") == 0 ) &&
			( packagetypeonly == NULL
			  || strcmp(packagetypeonly,"dsc") == 0 )
			) {
		fprintf(stderr,"I don't know what to do with a source-upload not containing a .dsc in '%s'!\n",filename);
		return RET_ERROR;
	}
	if( havedsc && !havediff && !havetar ) {
		fprintf(stderr,"I don't know what to do having a .dsc without a .diff.gz or .tar.gz in '%s'!\n",filename);
		return RET_ERROR;
	}

	return r;
}

static retvalue changes_checkfiles(filesdb filesdb,const char *filename,struct changes *changes) {
	struct fileentry *e;
	retvalue r;

	r = RET_NOTHING;

	for( e = changes->files; e != NULL ; e = e->next ) {
		//TODO: decide earlier which files to include
		if( FE_SOURCE(e->type) ) {
			assert(changes->srcdirectory!=NULL);
			e->filekey = calc_dirconcat(changes->srcdirectory,e->basename);
		} else {
			char *directory;

			// TODO: make this in-situ?
			/* as the directory depends on the sourcename, it can be
			 * different for every file... */
			directory = calc_sourcedir(e->component,changes->source);
			if( directory == NULL )
				return RET_ERROR_OOM;
			e->filekey = calc_dirconcat(directory,e->basename);
			free(directory);
		}

		if( e->filekey == NULL )
			return RET_ERROR_OOM;
		/* do not copy yet, but only check if it could be included */
		r = files_expect(filesdb,e->filekey,e->md5sum);
		if( RET_WAS_ERROR(r) )
			return r;
		/* If is was already there, remember that */
		if( RET_IS_OK(r) ) {
			e->wasalreadythere = TRUE;
		} else {
		/* and if it needs inclusion check if there is a file */
			char *fullfilename;

			assert( r == RET_NOTHING );
			// TODO: add a --paranoid to also check md5sums before copying?

			fullfilename = calc_dirconcat(changes->incomingdirectory,e->basename);
			if( fullfilename == NULL )
				return RET_ERROR_OOM;
			if( !isregularfile(fullfilename) ) {
				fprintf(stderr, "Cannot find file '%s' needed by '%s'!\n", fullfilename,filename);
				free(fullfilename);
				return RET_ERROR_MISSING;
			}
			free(fullfilename);
		}
	}

	return RET_OK;
}

static retvalue changes_includefiles(filesdb filesdb,struct changes *changes,int delete) {
	struct fileentry *e;
	retvalue r;

	r = RET_NOTHING;

	for( e = changes->files; e != NULL ; e = e->next ) {
		assert( e->filekey != NULL );

		if( e->wasalreadythere )
			continue;

		r = files_includefile(filesdb,changes->incomingdirectory,
				e->basename,e->filekey,e->md5sum,NULL,
				/* do not delete, we do that later outself */
				(delete>=D_MOVE)?D_COPY:delete);
		if( RET_IS_OK(r) )
			e->included = TRUE;
		if( RET_WAS_ERROR(r) )
			return r;
	}

	return r;
}
/* run if packages are not all includeable and the stuff put into the
 * pool shall be removed again */
static void changes_unincludefiles(filesdb filesdb,struct changes *changes) {
	struct fileentry *e;

	for( e = changes->files; e != NULL ; e = e->next ) {

		if( e->filekey == NULL || e->wasalreadythere || !e->included )
			continue;

		(void)files_deleteandremove(filesdb,e->filekey,TRUE,FALSE);
	}
}
/* delete the files included */
static retvalue changes_deleteleftoverfiles(struct changes *changes,int delete) {
	struct fileentry *e;
	retvalue result,r;

	if( delete < D_MOVE )
		return RET_OK;

	result = RET_OK;
	// TODO: we currently only see files included here, so D_DELETE
	// only affacts the .changes file.

	for( e = changes->files; e != NULL ; e = e->next ) {
		char *fullorigfilename;

		if( delete < D_DELETE && e->filekey == NULL )
			continue;

		fullorigfilename = calc_dirconcat(changes->incomingdirectory,
					e->basename);

		if( unlink(fullorigfilename) != 0 ) {
			int e = errno;
			fprintf(stderr, "Error deleting '%s': %d=%s\n",
					fullorigfilename, e, strerror(e));
			r = RET_ERRNO(e);
			RET_UPDATE(result,r);
		}
		free(fullorigfilename);
	}

	return result;
}

static retvalue changes_checkpkgs(filesdb filesdb,struct distribution *distribution,struct changes *changes, const char *sourcedirectory) {
	struct fileentry *e;
	retvalue r;

	r = RET_NOTHING;

	e = changes->files;
	while( e != NULL ) {
		char *fullfilename;
		if( e->type != fe_DEB && e->type != fe_DSC && e->type != fe_UDEB) {
			e = e->next;
			continue;
		}
		fullfilename = files_calcfullfilename(filesdb,e->filekey);
		if( fullfilename == NULL )
			return RET_ERROR_OOM;
		if( e->type == fe_DEB ) {
			r = deb_prepare(&e->pkg.deb,filesdb,
				e->component,e->architecture,
				e->section,e->priority,
				"deb",
				distribution,fullfilename,
				e->filekey,e->md5sum,
				D_INPLACE,FALSE,
				&changes->binaries,
				changes->source,changes->sourceversion);
		} else if( e->type == fe_UDEB ) {
			r = deb_prepare(&e->pkg.deb,filesdb,
				e->component,e->architecture,
				e->section,e->priority,
				"udeb",
				distribution,fullfilename,
				e->filekey,e->md5sum,
				D_INPLACE,FALSE,
				&changes->binaries,
				changes->source,changes->sourceversion);
		} else if( e->type == fe_DSC ) {
			if( !changes->isbinnmu || IGNORING_(dscinbinnmu,
"File '%s' looks like a source package, but this .changes looks like a binNMU\n"
"(as '%s' (from Source:) and '%s' (From Version:) differ.)\n",
				e->filekey, changes->sourceversion,
				changes->changesversion) ) {

				assert(changes->srccomponent!=NULL);
				assert(changes->srcdirectory!=NULL);
				r = dsc_prepare(&e->pkg.dsc,filesdb,
						changes->srccomponent,e->section,e->priority,
						distribution,sourcedirectory,fullfilename,
						e->filekey,e->basename,
						changes->srcdirectory,e->md5sum,
						D_INPLACE,
						changes->source,changes->sourceversion);
			} else
				r = RET_ERROR;
		}

		free(fullfilename);
		if( RET_WAS_ERROR(r) )
			break;
		e = e->next;
	}

	return r;
}
static retvalue changes_includepkgs(const char *dbdir,references refs,struct distribution *distribution,struct changes *changes,/*@null@*/struct strlist *dereferencedfilekeys, /*@null@*/struct trackingdata *trackingdata,bool_t *missed_p) {
	struct fileentry *e;
	retvalue result,r;

	*missed_p = FALSE;
	r = distribution_prepareforwriting(distribution);
	if( RET_WAS_ERROR(r) )
		return r;

	result = RET_NOTHING;

	e = changes->files;
	while( e != NULL ) {
		if( e->type != fe_DEB && e->type != fe_DSC && e->type != fe_UDEB) {
			e = e->next;
			continue;
		}
		if( interrupted() ) {
			return RET_ERROR_INTERUPTED;
		}
		if( e->type == fe_DEB ) {
			r = deb_addprepared(e->pkg.deb,dbdir,refs,
				e->architecture,"deb",
				distribution,dereferencedfilekeys,trackingdata);
			if( r == RET_NOTHING )
				*missed_p = TRUE;
		} else if( e->type == fe_UDEB ) {
			r = deb_addprepared(e->pkg.deb,dbdir,refs,
				e->architecture,"udeb",
				distribution,dereferencedfilekeys,trackingdata);
			if( r == RET_NOTHING )
				*missed_p = TRUE;
		} else if( e->type == fe_DSC ) {
			r = dsc_addprepared(e->pkg.dsc,dbdir,refs,
				distribution,dereferencedfilekeys,trackingdata);
			if( r == RET_NOTHING )
				*missed_p = TRUE;
		}
		RET_UPDATE(result, r);

		if( RET_WAS_ERROR(r) )
			break;
		e = e->next;
	}

	logger_wait();

	return result;
}

static bool_t permissionssuffice(UNUSED(struct changes *changes),
                                 const struct uploadpermissions *permissions) {
	return permissions->allowall;
}

/* insert the given .changes into the mirror in the <distribution>
 * if forcecomponent, forcesection or forcepriority is NULL
 * get it from the files or try to guess it. */
retvalue changes_add(const char *dbdir,trackingdb const tracks,references refs,filesdb filesdb,const char *packagetypeonly,const char *forcecomponent,const char *forcearchitecture,const char *forcesection,const char *forcepriority,struct distribution *distribution,const char *changesfilename,int delete,struct strlist *dereferencedfilekeys) {
	retvalue result,r;
	struct changes *changes;
	struct trackingdata trackingdata;
	char *directory;
	bool_t somethingwasmissed;

	r = changes_read(changesfilename,&changes,packagetypeonly,forcearchitecture);
	if( RET_WAS_ERROR(r) )
		return r;

	if( (distribution->suite == NULL ||
		!strlist_in(&changes->distributions,distribution->suite)) &&
	    !strlist_in(&changes->distributions,distribution->codename) &&
	    !strlist_intersects(&changes->distributions,
	                       &distribution->alsoaccept) ) {
		if( !IGNORING("Ignoring","To ignore",wrongdistribution,".changes put in a distribution not listed within it!\n") ) {
			changes_free(changes);
			return RET_ERROR;
		}
	}

	/* make sure caller has called distribution_loaduploaders */
	assert( distribution->uploaders == NULL || distribution->uploaderslist != NULL );
	if( distribution->uploaderslist != NULL ) {
		const struct uploadpermissions *permissions;
		int i;

		if( changes->fingerprints.count == 0 ) {
			r = uploaders_unsignedpermissions(distribution->uploaderslist,
					&permissions);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) ) {
				changes_free(changes);
				return r;
			}
			if( permissions == NULL || !permissionssuffice(changes,permissions) )
				permissions = NULL;
		}
		for( i = 0; i < changes->fingerprints.count ; i++ ) {
			const char *fingerprint = changes->fingerprints.values[i];
			r = uploaders_permissions(distribution->uploaderslist,
					fingerprint, &permissions);
			assert( r != RET_NOTHING );
			if( RET_WAS_ERROR(r) ) {
				changes_free(changes);
				return r;
			}
			if( permissions != NULL && permissionssuffice(changes,permissions) )
				break;
			permissions = NULL;
		}
		if( permissions == NULL &&
		    !IGNORING_(uploaders,"No rule allowing this package in found in %s!\n",
			    distribution->uploaders) ) {
			changes_free(changes);
			return RET_ERROR;
		}
	}

	if( IGNORABLE(missingfile) ) {
		r = dirs_getdirectory(changesfilename,&directory);
		if( RET_WAS_ERROR(r) ) {
			changes_free(changes);
			return r;
		}
	} else
		directory = NULL;


	/* look for component, section and priority to be correct or guess them*/
	r = changes_fixfields(distribution,changesfilename,changes,forcecomponent,forcesection,forcepriority);

	/* do some tests if values are sensible */
	if( !RET_WAS_ERROR(r) )
		r = changes_check(changesfilename,changes,forcearchitecture,packagetypeonly);

	if( interrupted() )
		RET_UPDATE(r,RET_ERROR_INTERUPTED);

	if( !RET_WAS_ERROR(r) )
		r = changes_checkfiles(filesdb,changesfilename,changes);

	if( interrupted() )
		RET_UPDATE(r,RET_ERROR_INTERUPTED);

	/* add files in the pool */
	if( !RET_WAS_ERROR(r) )
		r = changes_includefiles(filesdb,changes,delete);

	if( !RET_WAS_ERROR(r) )
		r = changes_checkpkgs(filesdb,distribution,changes,directory);

	free(directory);

	if( RET_WAS_ERROR(r) ) {
		changes_unincludefiles(filesdb,changes);
		changes_free(changes);
		return r;
	}

	if( tracks != NULL ) {
		r = trackingdata_summon(tracks,changes->source,changes->sourceversion,&trackingdata);
		if( RET_WAS_ERROR(r) ) {
			changes_free(changes);
			return r;
		}
		if( distribution->trackingoptions.includechanges ) {
			char *basename;
			assert( changes->srcdirectory != NULL );

			basename = calc_changes_basename(changes->source, changes->changesversion, &changes->architectures);
			changes->changesfilekey =
				calc_dirconcat(changes->srcdirectory,basename);
			free(basename);
			if( changes->changesfilekey == NULL ) {
				changes_free(changes);
				trackingdata_done(&trackingdata);
				return RET_ERROR_OOM;
			}
			if( interrupted() )
				r = RET_ERROR_INTERUPTED;
			else
			/* always D_COPY, and only delete it afterwards... */
				r = files_include(filesdb,changesfilename,
					changes->changesfilekey,
					NULL,NULL,D_COPY);
			if( RET_WAS_ERROR(r) ) {
				changes_free(changes);
				trackingdata_done(&trackingdata);
				return r;
			}
		}
	}
	if( interrupted() ) {
		if( tracks != NULL )
			trackingdata_done(&trackingdata);
		changes_free(changes);
		return RET_ERROR_INTERUPTED;
	}

	/* add the source and binary packages in the given distribution */
	result = changes_includepkgs(dbdir,refs,
		distribution,changes,dereferencedfilekeys,
		(tracks!=NULL)?&trackingdata:NULL, &somethingwasmissed);

	if( RET_WAS_ERROR(result) ) {
		if( tracks != NULL ) {
			trackingdata_done(&trackingdata);
		}
		changes_free(changes);
		return result;
	}

	if( tracks != NULL ) {
		if( changes->changesfilekey != NULL ) {
			char *changesfilekey = strdup(changes->changesfilekey);
			assert( changes->srcdirectory != NULL );
			if( changesfilekey == NULL ) {
				trackingdata_done(&trackingdata);
				changes_free(changes);
				return RET_ERROR_OOM;
			}

			r = trackedpackage_addfilekey(tracks,trackingdata.pkg,ft_CHANGES,changesfilekey,FALSE,refs);
			RET_ENDUPDATE(result,r);
		}
		r = trackingdata_finish(tracks, &trackingdata, refs, dereferencedfilekeys);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(result) ) {
			changes_free(changes);
			return result;
		}
	}

	/* if something was included, call --changes notify scripts */
	if( RET_IS_OK(result) ) {
		assert( logger_isprepared(distribution->logger) );
		logger_logchanges(distribution->logger, distribution->codename,
			changes->source, changes->changesversion, changes->control,
			changesfilename, changes->changesfilekey);
	}
	/* wait for notify scripts (including those for the packages)
	 * before deleting the .changes */
	logger_wait();

	if( (delete >= D_MOVE && changes->changesfilekey != NULL) ||
			delete >= D_DELETE ) {
		if( somethingwasmissed && delete < D_DELETE ) {
			if( verbose >= 0 ) {
				fprintf(stderr,"Not deleting '%s' as no package was added or some package was missed.\n(Use --delete --delete to delete anyway in such cases)\n",changesfilename);
			}
		} else {
			if( verbose >= 5 ) {
				printf("Deleting '%s'.\n",changesfilename);
			}
			if( unlink(changesfilename) != 0 ) {
				fprintf(stderr,"Error deleting '%s': %m\n",changesfilename);
			}
		}
	}
	result = changes_deleteleftoverfiles(changes, delete);
	(void)changes_free(changes);

	return result;
}
