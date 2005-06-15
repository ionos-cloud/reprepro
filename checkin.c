/*  This file is part of "reprepro"
 *  Copyright (C) 2003,2004,2005 Bernhard R. Link
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
#include "tracking.h"
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
	/* set between checkpkg and includepkg */
	union { struct dscpackage *dsc; struct debpackage *deb;} pkg;
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
	/* != NULL if changesfile was put into pool/ */
	/*@null@*/ char *changesfilekey;
	/* the directory where source files are put into */
	char *srcdirectory;
	/* (only to warn if multiple are used) */
	const char *firstcomponent;
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
		free(entry);
		entry = h;
	}
}

static void changes_free(/*@only@*/struct changes *changes) {
	if( changes != NULL ) {
		free(changes->source);
		free(changes->version);
		strlist_done(&changes->architectures);
		strlist_done(&changes->binaries);
		freeentries(changes->files);
		strlist_done(&changes->distributions);
		free(changes->control);
		free(changes->srcdirectory);
		free(changes->changesfilekey);
//		trackedpackage_free(changes->trackedpkg);
	}
	free(changes);
}


static retvalue newentry(struct fileentry **entry,const char *fileline,const char *packagetypeonly,const char *forcearchitecture, const char *sourcename) {
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
		fprintf(stderr,"Unexpected sixth argument in '%s'!\n",fileline);
		return RET_ERROR;
	}
	if( *md5start == '\0' || *sizestart == '\0' || *sectionstart == '\0'
			|| *priostart == '\0' || *filestart == '\0' ) {
		fprintf(stderr,"Not five arguments in '%s'!\n",fileline);
		return RET_ERROR;
	}

	p = filestart;
	while( *p != '\0' && *p != '_' && !xisspace(*p) )
		p++;
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
	// but as the packagetypes would be valid part of the version, too,
	// this check gets the broken things. 
	names_overversion(&p,TRUE);
	if( *p != '\0' && *p != '_' ) {
		fprintf(stderr,"Unexpected character '%c' in filename within '%s'!\n",*p,fileline);
		return RET_ERROR;
	}
	if( *p == '_' ) {
		/* Things having a underscole will have an architecture
		 * and be either .deb or .udeb */
		p++;
		archstart = p;
		while( *p !='\0' && *p != '.' )
			p++;
		if( *p != '.' ) {
			fprintf(stderr,"Expect something of the form name_version_arch.[u]deb but got '%s'!\n",filestart);
			return RET_ERROR;
		}
		archend = p;
		p++;
		typestart = p;
		while( *p !='\0' && !xisspace(*p) )
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
		 * to look for the packagetype ourself... */
		while( *p !='\0' && !xisspace(*p) ) {
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
	if( FE_SOURCE(type) && packagetypeonly != NULL && strcmp(packagetypeonly,"dsc")!=0)
		return RET_NOTHING;
	if( type == fe_DEB && packagetypeonly != NULL && strcmp(packagetypeonly,"deb")!=0)
		return RET_NOTHING;
	if( type == fe_UDEB && packagetypeonly != NULL && strcmp(packagetypeonly,"udeb")!=0)
		return RET_NOTHING;

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

	if( e->basename == NULL || e->md5sum == NULL || e->section == NULL || 
	    e->priority == NULL || e->architecture == NULL || e->name == NULL ) {
		freeentries(e);
		return RET_ERROR_OOM;
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

static retvalue check(const char *filename,struct changes *changes,const char *field,int force) {
	retvalue r;

	r = chunk_checkfield(changes->control,field);
	if( r == RET_NOTHING ) {
		fprintf(stderr,"In '%s': Missing '%s' field!\n",filename,field);
		if( force <= 0 )
			return RET_ERROR;
	}
	return r;
}

static retvalue changes_read(const char *filename,/*@out@*/struct changes **changes,/*@null@*/const char *packagetypeonly,/*@null@*/const char *forcearchitecture,int force, bool_t onlysigned) {
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
	r = signature_readsignedchunk(filename,&c->control,onlysigned);
	R;
	r = check(filename,c,"Format",force);
	R;
	r = check(filename,c,"Date",force);
	R;
	r = chunk_getname(c->control,"Source",&c->source,FALSE);
	E("Missing 'Source' field");
	r = propersourcename(c->source);
	R;
	r = chunk_getwordlist(c->control,"Binary",&c->binaries);
	E("Missing 'Binary' field");
	r = chunk_getwordlist(c->control,"Architecture",&c->architectures);
	E("Missing 'Architecture' field");
	r = chunk_getvalue(c->control,"Version",&c->version);
	E("Missing 'Version' field");
	r = properversion(c->version);
	E("Malforce Version number");
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
	r = changes_parsefilelines(filename,c,&filelines,packagetypeonly,forcearchitecture);
	strlist_done(&filelines);
	R;

	*changes = c;
	return RET_OK;
#undef E
#undef C
#undef R
}

static retvalue changes_fixfields(const struct distribution *distribution,const char *filename,struct changes *changes,/*@null@*/const char *forcecomponent,/*@null@*/const char *forcesection,/*@null@*/const char *forcepriority,const struct alloverrides *ao) {
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
			FE_BINARY(e->type)?(e->type==fe_UDEB?ao->udeb:ao->deb):ao->dsc,
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
			fprintf(stderr,"No section specified for '%s'!\n",filename);
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
		fprintf(stderr,"Architecture-header lists architecture '%s', but no files for this!\n",architecture);
		return RET_ERROR;
	}
	return RET_OK;
}

static retvalue changes_check(const char *filename,struct changes *changes,/*@null@*/const char *forcearchitecture) {
	int i;
	struct fileentry *e;
	retvalue r = RET_OK;
	bool_t havedsc=FALSE, haveorig=FALSE, havetar=FALSE, havediff=FALSE;
	
	/* First check for each given architecture, if it has files: */
	if( forcearchitecture != NULL ) {
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
	while( e != NULL ) {
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
			havedsc = TRUE;
			calculatedname = calc_source_basename(changes->source,changes->version);
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

static retvalue changes_includefiles(filesdb filesdb,const char *filename,struct changes *changes,int delete) {
	struct fileentry *e;
	retvalue r;
	char *sourcedir; 

	r = dirs_getdirectory(filename,&sourcedir);
	if( RET_WAS_ERROR(r) )
		return r;

	r = RET_NOTHING;

	e = changes->files;
	while( e != NULL ) {
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

static retvalue changes_checkpkgs(filesdb filesdb,struct distribution *distribution,struct changes *changes,const struct alloverrides *ao, bool_t onlysigned) {
	struct fileentry *e;
	retvalue r;
	bool_t somethingwasmissed = FALSE;

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
				ao->deb,D_INPLACE);
			if( r == RET_NOTHING )
				somethingwasmissed = TRUE;
		} else if( e->type == fe_UDEB ) {
			r = deb_prepare(&e->pkg.deb,filesdb,
				e->component,e->architecture,
				e->section,e->priority,
				"udeb",
				distribution,fullfilename,
				e->filekey,e->md5sum,
				ao->udeb,D_INPLACE);
			if( r == RET_NOTHING )
				somethingwasmissed = TRUE;
		} else if( e->type == fe_DSC ) {
			assert(changes->srccomponent!=NULL);
			assert(changes->srcdirectory!=NULL);
			r = dsc_prepare(&e->pkg.dsc,filesdb,
				changes->srccomponent,e->section,e->priority,
				distribution,fullfilename,
				e->filekey,e->basename,
				changes->srcdirectory,e->md5sum,
				ao->dsc,D_INPLACE,onlysigned);
			if( r == RET_NOTHING )
				somethingwasmissed = TRUE;
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
static retvalue changes_includepkgs(const char *dbdir,references refs,struct distribution *distribution,struct changes *changes,int force,/*@null@*/struct strlist *dereferencedfilekeys, /*@null@*/struct trackingdata *trackingdata) {
	struct fileentry *e;
	retvalue r;
	bool_t somethingwasmissed = FALSE;

	r = RET_NOTHING;

	e = changes->files;
	while( e != NULL ) {
		if( e->type != fe_DEB && e->type != fe_DSC && e->type != fe_UDEB) {
			e = e->next;
			continue;
		}
		if( e->type == fe_DEB ) {
			r = deb_addprepared(e->pkg.deb,dbdir,refs,
				e->architecture,"deb",
				distribution,force,dereferencedfilekeys,trackingdata);
			if( r == RET_NOTHING )
				somethingwasmissed = TRUE;
		} else if( e->type == fe_UDEB ) {
			r = deb_addprepared(e->pkg.deb,dbdir,refs,
				e->architecture,"udeb",
				distribution,force,dereferencedfilekeys,trackingdata);
			if( r == RET_NOTHING )
				somethingwasmissed = TRUE;
		} else if( e->type == fe_DSC ) {
			r = dsc_addprepared(e->pkg.dsc,dbdir,refs,
				distribution,force,dereferencedfilekeys,trackingdata);
			if( r == RET_NOTHING )
				somethingwasmissed = TRUE;
		}
		
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
retvalue changes_add(const char *dbdir,trackingdb const tracks,references refs,filesdb filesdb,const char *packagetypeonly,const char *forcecomponent,const char *forcearchitecture,const char *forcesection,const char *forcepriority,struct distribution *distribution,const struct alloverrides *ao,const char *changesfilename,int force,int delete,struct strlist *dereferencedfilekeys,bool_t onlysigned) {
	retvalue result,r;
	struct changes *changes;
	struct trackingdata trackingdata;

	r = changes_read(changesfilename,&changes,packagetypeonly,forcearchitecture,force,onlysigned);
	if( RET_WAS_ERROR(r) )
		return r;

	if( (distribution->suite == NULL || 
		!strlist_in(&changes->distributions,distribution->suite)) &&
	    !strlist_in(&changes->distributions,distribution->codename) ) {
		fprintf(stderr,"Warning: .changes put in a distribution not listed within it!\n");
	}

	/* look for component, section and priority to be correct or guess them*/
	r = changes_fixfields(distribution,changesfilename,changes,forcecomponent,forcesection,forcepriority,ao);

	/* do some tests if values are sensible */
	if( !RET_WAS_ERROR(r) )
		r = changes_check(changesfilename,changes,forcearchitecture);

	/* add files in the pool */
	//TODO: D_DELETE would fail here, what to do?
	if( !RET_WAS_ERROR(r) )
		r = changes_includefiles(filesdb,changesfilename,changes,delete);

	if( !RET_WAS_ERROR(r) )
		r = changes_checkpkgs(filesdb,distribution,changes,ao,onlysigned);


	if( RET_WAS_ERROR(r) ) {
		changes_free(changes);
		return r;
	}

	if( tracks != NULL ) {
		r = trackingdata_summon(tracks,changes->source,changes->version,&trackingdata);
		if( RET_WAS_ERROR(r) ) {
			changes_free(changes);
			return r;
		}
		if( distribution->trackingoptions.includechanges ) {
			const char *basename;
			assert( changes->srcdirectory != NULL );

			basename = dirs_basename(changesfilename);
			changes->changesfilekey = 
				calc_dirconcat(changes->srcdirectory,basename);
			if( changes->changesfilekey == NULL ) {
				changes_free(changes);
				trackingdata_done(&trackingdata);
				return RET_ERROR_OOM;
			}
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

	/* add the source and binary packages in the given distribution */
	result = changes_includepkgs(dbdir,refs,
		distribution,changes,force,dereferencedfilekeys,
		(tracks!=NULL)?&trackingdata:NULL);

	if( RET_WAS_ERROR(r) ) {
		if( tracks != NULL ) {
			trackingdata_done(&trackingdata);
		}
		changes_free(changes);
		return r;
	}

	if( tracks != NULL ) {
		if( changes->changesfilekey != NULL ) {
			assert( changes->srcdirectory != NULL );

			r = trackedpackage_addfilekey(tracks,trackingdata.pkg,ft_CHANGES,changes->changesfilekey,refs);
			RET_ENDUPDATE(result,r);
		}
		if( trackingdata.isnew ) {
			r = tracking_put(tracks,trackingdata.pkg);
		} else {
			r = tracking_replace(tracks,trackingdata.pkg);
		}
		trackingdata_done(&trackingdata);
		RET_ENDUPDATE(result,r);
		if( RET_WAS_ERROR(result) ) {
			changes_free(changes);
			return result;
		}
	}

	if( delete >= D_MOVE && changesfilename != NULL ) {
		if( result == RET_NOTHING && delete < D_DELETE && 
				changes->changesfilekey == NULL) {
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
	//TODO: why is here no changes_free?

	return RET_OK;
}
