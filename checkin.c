/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include <db.h>
#include "error.h"
#include "strlist.h"
#include "md5sum.h"
#include "names.h"
#include "dirs.h"
#include "chunks.h"
#include "reference.h"
#include "signature.h"
#include "sources.h"
#include "files.h"
#include "guesscomponent.h"
#include "override.h"
#include "checkindsc.h"
#include "checkindeb.h"
#include "checkin.h"

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

typedef	enum { fe_UNKNOWN=0,fe_DEB,fe_UDEB,fe_DSC,fe_DIFF,fe_ORIG,fe_TAR} filetype;

#define FE_BINARY(ft) ( (ft) == fe_DEB || (ft) == fe_UDEB )
#define FE_SOURCE(ft) ( (ft) == fe_DIFF || (ft) == fe_ORIG || (ft) == fe_TAR || (ft) == fe_DSC || (ft) == fe_UNKNOWN)

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
};

struct changes {
	/* Things read by changes_read: */
	char *source, *version;
	struct strlist distributions,
		       architectures,
		       binaries;
	struct fileentry *files;
	char *control;
	/* Things to be set by changes_fixfields: */
	/* the component source files are put into */
	const char *srccomponent;
	/* the directory where source files are put into */
	char *srcdirectory;
	/* (only to warn if multiple are used) */
	const char *firstcomponent;
};

static void freeentries(struct fileentry *entry) {
	struct fileentry *h;

	while( entry ) {
		h = entry->next;
		free(entry->filekey);
		free(entry->component);
		free(entry->basename);
		free(entry->md5sum);
		free(entry->section);
		free(entry->priority);
		free(entry->architecture);
		free(entry->name);
		free(entry);
		entry = h;
	}
}

static void changes_free(struct changes *changes) {
	if( changes != NULL ) {
		free(changes->source);
		free(changes->version);
		strlist_done(&changes->architectures);
		strlist_done(&changes->binaries);
		freeentries(changes->files);
		strlist_done(&changes->distributions);
		free(changes->control);
		free(changes->srcdirectory);
	}
	free(changes);
}


static retvalue newentry(struct fileentry **entry,const char *fileline,const char *forcearchitecture, const char *sourcename) {
	struct fileentry *e;
	const char *p,*md5start,*md5end;
	const char *sizestart,*sizeend;
	const char *sectionstart,*sectionend;
	const char *priostart,*prioend;
	const char *filestart,*nameend,*fileend;
	const char *archstart,*archend;
	const char *versionstart,*typestart;
	filetype type;

	p = fileline;
	while( *p && isspace(*p) )
		p++;
	md5start = p;
	while( *p && !isspace(*p) )
		p++;
	md5end = p;
	while( *p && isspace(*p) )
		p++;
	sizestart = p;
	while( *p && !isspace(*p) )
		p++;
	sizeend = p;
	while( *p && isspace(*p) )
		p++;
	sectionstart = p;
	while( *p && !isspace(*p) )
		p++;
	sectionend = p;
	while( *p && isspace(*p) )
		p++;
	priostart = p;
	while( *p && !isspace(*p) )
		p++;
	prioend = p;
	while( *p && isspace(*p) )
		p++;
	filestart = p;
	while( *p && !isspace(*p) )
		p++;
	fileend = p;
	while( *p && isspace(*p) )
		p++;
	if( *p != '\0' ) {
		fprintf(stderr,"Unexpected sixth argument in '%s'!\n",fileline);
		return RET_ERROR;
	}
	if( *md5start == '\0' || *sizestart == '\0' || *sectionstart == '\0'
			|| *priostart == '\0' || *filestart == '\0' ) {
		fprintf(stderr,"Not five arguments in '%s'!\n",fileline);
		return RET_ERROR;
	}

	p = filestart;
	names_overpkgname(&p);
	if( *p != '_' ) {
		if( *p == '\0' )
			fprintf(stderr,"No underscore in filename in '%s'!",fileline);
		else
			fprintf(stderr,"Unexpected character '%c' in filename in '%s'\n!",*p,fileline);
		return RET_ERROR;
	}
	nameend = p;
	p++;
	versionstart = p;
	// We cannot say where the version ends and the filename starts,
	// but as the suffixes would be valid part of the version, too,
	// this check gets the broken things. 
	names_overversion(&p);
	if( *p != '\0' && *p != '_' ) {
		fprintf(stderr,"Unexpected character '%c' in filename within '%s'!\n",*p,fileline);
		return RET_ERROR;
	}
	if( *p == '_' ) {
		/* Things having a underscole will have an architecture
		 * and be either .deb or .udeb */
		p++;
		archstart = p;
		while( *p && *p != '.' )
			p++;
		if( *p != '.' ) {
			fprintf(stderr,"Expect something of the form name_version_arch.[u]deb but got '%s'!\n",filestart);
			return RET_ERROR;
		}
		archend = p;
		p++;
		typestart = p;
		while( *p && !isspace(*p) )
			p++;
		if( p-typestart == 3 && strncmp(typestart,"deb",3) == 0 )
			type = fe_DEB;
		else if( p-typestart == 4 && strncmp(typestart,"udeb",4) == 0 )
			type = fe_UDEB;
		else {
			fprintf(stderr,"'%s' looks neighter like .deb nor like .udeb!\n",filestart);
			return RET_ERROR;
		}
		if( strncmp(archstart,"source",6) == 0 ) {
			fprintf(stderr,"How can a .[u]deb be architecture 'source'?('%s')\n",filestart);
			return RET_ERROR;
		}
	} else {
		/* this looks like some source-package, we will have
		 * to look for the suffix ourself... */
		while( *p && !isspace(*p) ) {
			p++;
		}
		if( p-versionstart > 12 && strncmp(p-12,".orig.tar.gz",12) == 0 )
			type = fe_ORIG;
		else if( p-versionstart > 7 && strncmp(p-7,".tar.gz",7) == 0 )
			type = fe_TAR;
		else if( p-versionstart > 8 && strncmp(p-8,".diff.gz",8) == 0 )
			type = fe_DIFF;
		else if( p-versionstart > 4 && strncmp(p-4,".dsc",4) == 0 )
			type = fe_DSC;
		else {
			type = fe_UNKNOWN;
			fprintf(stderr,"Unknown filetype: '%s', assuming to be source format...\n",fileline);
		}
		archstart = "source";
		archend = archstart + 6;
		if( strncmp(filestart,sourcename,nameend-filestart) != 0 ) {
			fprintf(stderr,"Warning: Strange file '%s'!\nLooks like source but does not start with '%s_' as I would have guessed!\nI hope you know what you do.\n",filestart,sourcename);
		}
	}
	/* now copy all those parts into the structure */
	e = calloc(1,sizeof(struct fileentry));
	if( e == NULL )
		return RET_ERROR_OOM;
	e->md5sum = names_concatmd5sumandsize(md5start,md5end,sizestart,sizeend);
	e->section = strndup(sectionstart,sectionend-sectionstart);
	e->priority = strndup(priostart,prioend-priostart);
	e->basename = strndup(filestart,fileend-filestart);
	e->architecture = strndup(archstart,archend-archstart);
	e->name = strndup(filestart,nameend-filestart);
	e->type = type;

	if( !e->basename || !e->md5sum || !e->section || !e->priority || !e->architecture || !e->name ) {
		freeentries(e);
		return RET_ERROR_OOM;
	}
	if( forcearchitecture ) {
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
static retvalue changes_parsefilelines(const char *filename,struct changes *changes,const struct strlist *filelines,const char *forcearchitecture,int force) {
	retvalue result,r;
	int i;

	assert( changes->files == NULL);
	result = RET_NOTHING;

	for( i = 0 ; i < filelines->count ; i++ ) {
		const char *fileline = filelines->values[i];

		r = newentry(&changes->files,fileline,forcearchitecture,changes->source);
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

static retvalue check(const char *filename,struct changes *changes,const char *field,int force) {
	retvalue r;

	r = chunk_checkfield(changes->control,field);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"In '%s': Missing '%s' field!\n",filename,field);
		if( !force )
			return RET_ERROR;
	}
	return r;
}

static retvalue changes_read(const char *filename,struct changes **changes,const char *forcearchitecture,int force) {
	retvalue r;
	struct changes *c;
	struct strlist filelines;

#define E(err,param...) { \
		if( r == RET_NOTHING ) { \
			fprintf(stderr,"In '%s': " err "\n",filename , ## param ); \
			r = RET_ERROR; \
	  	} \
		if( RET_WAS_ERROR(r) ) { \
			changes_free(c); \
			return r; \
		} \
	}
#define C(err,param...) { \
		if( RET_WAS_ERROR(r) ) { \
			if( !force ) { \
				fprintf(stderr,"In '%s': " err "\n",filename , ## param ); \
				changes_free(c); \
				return r; \
			} else { \
				fprintf(stderr,"Ignoring " err " in '%s' due to --force:\n " err "\n" , ## param , filename); \
			} \
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
	r = signature_readsignedchunk(filename,&c->control);
	R;
	r = check(filename,c,"Format",force);
	R;
	r = check(filename,c,"Date",force);
	R;
	r = chunk_getname(c->control,"Source",&c->source,0);
	E("Missing 'Source' field");
	r = names_checkpkgname(c->source);
	C("Malforce Source-field");
	r = chunk_getwordlist(c->control,"Binary",&c->binaries);
	E("Missing 'Binary' field");
	r = chunk_getwordlist(c->control,"Architecture",&c->architectures);
	E("Missing 'Architecture' field");
	r = chunk_getvalue(c->control,"Version",&c->version);
	E("Missing 'Version' field");
	r = names_checkversion(c->version);
	C("Malforce Version number");
	r = chunk_getwordlist(c->control,"Distribution",&c->distributions);
	E("Missing 'Distribution' field");
	r = check(filename,c,"Urgency",force);
	R;
	r = check(filename,c,"Maintainer",force);
	R;
	r = check(filename,c,"Description",force);
	R;
	r = check(filename,c,"Changes",force);
	R;
	r = chunk_getextralinelist(c->control,"Files",&filelines);
	E("Missing 'Files' field");
	r = changes_parsefilelines(filename,c,&filelines,forcearchitecture,force);
	strlist_done(&filelines);
	R;

	*changes = c;
	return RET_OK;
#undef E
#undef C
#undef R
}

static retvalue changes_fixfields(const struct distribution *distribution,const char *filename,struct changes *changes,const char *forcecomponent,const char *forcesection,const char *forcepriority,const struct overrideinfo *srcoverride,const struct overrideinfo *override,int force) {
	struct fileentry *e;
	retvalue r;

	e = changes->files;

	if( e == NULL ) {
		fprintf(stderr,"No files given in '%s'!\n",filename);
		return RET_ERROR;
	}
	
	while( e ) {
		const struct overrideinfo *oinfo = NULL;
		const char *force = NULL;
		if( !forcesection || !forcepriority ) {
			oinfo = override_search(
					FE_BINARY(e->type)?override:srcoverride,
					e->name);
		}
		
		if( forcesection ) 
			force = forcesection;
		else
			force = override_get(oinfo,SECTION_FIELDNAME);
		if( force ) {
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
			fprintf(stderr,"No section specified for '%s'!\n",filename);
			return RET_ERROR;
		}
		if( forcepriority )
			force = forcepriority;
		else
			force = override_get(oinfo,PRIORITY_FIELDNAME);
		if( force ) {
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
			if( changes->srccomponent == NULL ) {
				changes->srccomponent = e->component;
			} else if( strcmp(changes->srccomponent,e->component) != 0)  {
				fprintf(stderr,"%s contains source files guessed to be in different components ('%s' vs '%s)!\n",filename,e->component,changes->firstcomponent);
				return RET_ERROR;
			}
		} else if( FE_BINARY(e->type) ){
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
	}

	return RET_OK;
}

static inline retvalue checkforarchitecture(const struct fileentry *e,const char *architecture ) {
	while( e && strcmp(e->architecture,architecture) != 0 )
		e = e->next;
	if( e == NULL ) {
		fprintf(stderr,"Architecture-header lists architecture '%s', but no files for this!\n",architecture);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue changes_check(const char *filename,struct changes *changes,const char *forcearchitecture,int force) {
	int i;
	struct fileentry *e;
	retvalue r = RET_OK;
	int havedsc=0, haveorig=0, havetar=0, havediff=0;
	
	/* First check for each given architecture, if it has files: */
	if( forcearchitecture ) {
		if( !strlist_in(&changes->architectures,forcearchitecture) ){
			fprintf(stderr,"Architecture-header does not list the"
				     " architecture '%s' to be forced in!\n",
					forcearchitecture);
			return RET_ERROR_MISSING;
		}
		r = checkforarchitecture(changes->files,forcearchitecture);
		if( RET_WAS_ERROR(r) )
			return r;
	} else {
		for( i = 0 ; i < changes->architectures.count ; i++ ) {
			r = checkforarchitecture(changes->files,
				changes->architectures.values[i]);
			if( RET_WAS_ERROR(r) )
				return r;
		}
	}
	/* Then check for each file, if its architecture is sensible
	 * and listed. */
	e = changes->files;
	while( e ) {
		if( !strlist_in(&changes->architectures,e->architecture) ) {
			fprintf(stderr,"'%s' looks like architecture '%s', but this is not listed in the Architecture-Header!\n",filename,e->architecture);
			r = RET_ERROR;
		}
		if( e->type == fe_DSC ) {
			char *calculatedname;
			if( havedsc ) {
				fprintf(stderr,"I don't know what to do with multiple .dsc files in '%s'!\n",filename);
				return RET_ERROR;
			}
			havedsc = 1;
			calculatedname = calc_source_basename(changes->source,changes->version);
			if( calculatedname == NULL )
				return RET_ERROR_OOM;
			if( strcmp(calculatedname,e->basename) != 0 ) {
				free(calculatedname);
				fprintf(stderr,"dsc-filename is '%s' instead of the expected '%s'!\n",e->basename,calculatedname);
				return RET_ERROR;
			}
			free(calculatedname);
		} else if( e->type == fe_DIFF ) {
			if( havediff ) {
				fprintf(stderr,"I don't know what to do with multiple .diff files in '%s'!\n",filename);
				return RET_ERROR;
			}
			havediff = 1;
		} else if( e->type == fe_ORIG ) {
			if( haveorig ) {
				fprintf(stderr,"I don't know what to do with multiple .orig.tar.gz files in '%s'!\n",filename);
				return RET_ERROR;
			}
			haveorig = 1;
		} else if( e->type == fe_TAR ) {
			if( havetar ) {
				fprintf(stderr,"I don't know what to do with multiple .tar.gz files in '%s'!\n",filename);
				return RET_ERROR;
			}
			havetar = 1;
		}

		e = e->next;
	}

	if( havetar && haveorig ) {
		fprintf(stderr,"I don't know what to do having a .tar.gz and a .orig.tar.gz in '%s'!\n",filename);
		return RET_ERROR;
	}
	if( havetar && havediff ) {
		fprintf(stderr,"I don't know what to do having a .tar.gz not beeing a .orig.tar.gz and a .diff.gz in '%s'!\n",filename);
		return RET_ERROR;
	}
	if( strlist_in(&changes->architectures,"source") && !havedsc ) {
		fprintf(stderr,"I don't know what to do with a source-upload not containing a .dsc in '%s'!\n",filename);
		return RET_ERROR;
	}
	if( havedsc && !havediff && !havetar ) {
		fprintf(stderr,"I don't know what to do having a .dsc without a .diff.gz or .tar.gz in '%s'!\n",filename);
		return RET_ERROR;
	}

	return r;
}

static retvalue changes_includefiles(filesdb filesdb,const char *filename,struct changes *changes,int force,int delete) {
	struct fileentry *e;
	retvalue r;
	char *sourcedir; 

	r = dirs_getdirectory(filename,&sourcedir);
	if( RET_WAS_ERROR(r) )
		return r;

	r = RET_NOTHING;

	e = changes->files;
	while( e ) {
		if( FE_SOURCE(e->type) ) {
			assert(changes->srcdirectory);
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

		if( e->filekey == NULL ) {
			free(sourcedir);
			return RET_ERROR_OOM;
		}
		r = files_includefile(filesdb,sourcedir,e->basename,e->filekey,e->md5sum,NULL,delete);
		if( RET_WAS_ERROR(r) )
			break;
		e = e->next;
	}

	free(sourcedir);
	return r;
}

static retvalue changes_includepkgs(const char *dbdir,DB *references,filesdb filesdb,struct distribution *distribution,struct changes *changes,const struct overrideinfo *srcoverride,const struct overrideinfo *binoverride,int force) {
	struct fileentry *e;
	retvalue r;
	int somethingwasmissed = 0;

	r = RET_NOTHING;

	e = changes->files;
	while( e ) {
		char *fullfilename;
		if( e->type != fe_DEB && e->type != fe_DSC && e->type != fe_UDEB) {
			e = e->next;
			continue;
		}
		fullfilename = calc_dirconcat(filesdb->mirrordir,e->filekey);
		if( fullfilename == NULL )
			return RET_ERROR_OOM;
		if( e->type == fe_DEB ) {
			r = deb_add(dbdir,references,filesdb,
				e->component,e->architecture,
				e->section,e->priority,
				"deb",
				distribution,fullfilename,
				e->filekey,e->md5sum,
				binoverride,
				force,D_INPLACE);
			if( r == RET_NOTHING )
				somethingwasmissed = 1;
		} else if( e->type == fe_UDEB ) {
			r = deb_add(dbdir,references,filesdb,
				e->component,e->architecture,
				e->section,e->priority,
				"udeb",
				distribution,fullfilename,
				e->filekey,e->md5sum,
				binoverride,
				force,D_INPLACE);
			if( r == RET_NOTHING )
				somethingwasmissed = 1;
		} else if( e->type == fe_DSC ) {
			assert(changes->srccomponent);
			assert(changes->srcdirectory);
			r = dsc_add(dbdir,references,filesdb,
				changes->srccomponent,e->section,e->priority,
				distribution,fullfilename,
				e->filekey,e->basename,
				changes->srcdirectory,e->md5sum,
				srcoverride,
				force,D_INPLACE);
			if( r == RET_NOTHING )
				somethingwasmissed = 1;
		}
		
		free(fullfilename);
		if( RET_WAS_ERROR(r) )
			break;
		e = e->next;
	}

	if( RET_IS_OK(r) && somethingwasmissed ) {
		return RET_NOTHING;
	}
	return r;
}

/* insert the given .changes into the mirror in the <distribution>
 * if forcecomponent, forcesection or forcepriority is NULL
 * get it from the files or try to guess it. */
retvalue changes_add(const char *dbdir,DB *references,filesdb filesdb,const char *forcecomponent,const char *forcearchitecture,const char *forcesection,const char *forcepriority,struct distribution *distribution,const struct overrideinfo *srcoverride,const struct overrideinfo *binoverride,const char *changesfilename,int force,int delete) {
	retvalue r;
	struct changes *changes;

	r = changes_read(changesfilename,&changes,forcearchitecture,force);
	if( RET_WAS_ERROR(r) )
		return r;
//	if( changes->distributions.count != 1 ) {
//		fprintf(stderr,"There is not exactly one distribution given!\n");
//		changes_free(changes);
//		return RET_ERROR;
//	}
	if( (distribution->suite == NULL || 
		!strlist_in(&changes->distributions,distribution->suite)) &&
	    !strlist_in(&changes->distributions,distribution->codename) ) {
		fprintf(stderr,"Warning: .changes put in a distribution not listed within it!\n");
	}
	/* look for component, section and priority to be correct or guess them*/
	r = changes_fixfields(distribution,changesfilename,changes,forcecomponent,forcesection,forcepriority,srcoverride,binoverride,force);
	if( RET_WAS_ERROR(r) ) {
		changes_free(changes);
		return r;
	}
	/* do some tests if values are sensible */
	r = changes_check(changesfilename,changes,forcearchitecture,force);
	if( RET_WAS_ERROR(r) ) {
		changes_free(changes);
		return r;
	}
	
	/* add files in the pool */
	//TODO: D_DELETE would fail here, what to do?
	r = changes_includefiles(filesdb,changesfilename,changes,force,delete);
	if( RET_WAS_ERROR(r) ) {
		changes_free(changes);
		return r;
	}

	/* add the source and binary packages in the given distribution */
	r = changes_includepkgs(dbdir,references,filesdb,
		distribution,changes,srcoverride,binoverride,force);
	if( RET_WAS_ERROR(r) ) {
		changes_free(changes);
		return r;
	}

	if( delete >= D_MOVE ) {
		if( r == RET_NOTHING && delete < D_DELETE ) {
			if( verbose >= 0 ) {
				fprintf(stderr,"Not deleting '%s' as no package was added or some package was missed.\n(Use --delete --delete to delete anyway in such cases)\n",changesfilename);
			}
		} else {
			if( verbose >= 5 ) {
				fprintf(stderr,"Deleting '%s'.\n",changesfilename);
			}
			if( unlink(changesfilename) != 0 ) {
				fprintf(stderr,"Error deleting '%s': %m\n",changesfilename);
			}
		}
	}

	return RET_OK;
}
