#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <malloc.h>
#include "packages.h"
#include "chunks.h"
#include "binaries.h"
#include "names.h"
#include "dpkgversions.h"

extern int verbose;
extern int force;

/* get somefields out of a "Packages.gz"-chunk. returns 1 on success, 0 if incomplete, -1 on error */
static int binaries_parse_chunk(const char *chunk,char **packagename,char **origfilename,char **sourcename,char **filename,char **md5andsize) {
	const char *f,*f2;
	char *pmd5,*psize,*ppackage;
#define IFREE(p) if(p) free(*p);

	f  = chunk_getfield("Package",chunk);
	if( !f ) {
		return 0;
	}
	ppackage = chunk_dupvalue(f);	
	if( !ppackage ) {
		return -1;
	}
	if( packagename ) {
		*packagename = ppackage;
	}

	if( origfilename ) {
		/* Read the filename given there */
		f = chunk_getfield("Filename",chunk);
		if( ! f ) {
			free(ppackage);
			return 0;
		}
		*origfilename = chunk_dupvalue(f);
		if( !*origfilename ) {
			free(ppackage);
			return -1;
		}
		if( verbose > 3 ) 
			fprintf(stderr,"got: %s\n",*origfilename);
	}


	/* collect the given md5sum and size */

	if( md5andsize ) {

		f = chunk_getfield("MD5sum",chunk);
		f2 = chunk_getfield("Size",chunk);
		if( !f || !f2 ) {
			free(ppackage);
			IFREE(origfilename);
			return 0;
		}
		pmd5 = chunk_dupvalue(f);
		psize = chunk_dupvalue(f2);
		if( !pmd5 || !psize ) {
			free(ppackage);
			free(psize);free(pmd5);
			IFREE(origfilename);
			return -1;
		}
		asprintf(md5andsize,"%s %s",pmd5,psize);
		free(pmd5);free(psize);
		if( !*md5andsize ) {
			free(ppackage);
			IFREE(origfilename);
			return -1;
		}
	}

	/* get the sourcename */

	if( sourcename ) {
		f  = chunk_getfield("Source",chunk);
		if( f )
			/* do something with the version here? */
			*sourcename = chunk_dupword(f);	
		else {
			*sourcename = strdup(ppackage);
		}
		if( !*sourcename ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			return -1;
		}
	}

	/* generate a filename based on package,version and architecture */

	if( filename ) {
		char *parch,*pversion,*v;

		f  = chunk_getfield("Version",chunk);
		f2 = chunk_getfield("Architecture",chunk);
		if( !f || !f2 ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			return 0;
		}
		pversion = chunk_dupvalue(f);
		parch = chunk_dupvalue(f2);
		if( !parch || !pversion ) {
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			return -1;
		}
		v = index(pversion,':');
		if( v )
			v++;
		else
			v = pversion;
		/* TODO check parts to contain out of save charakters */
		*filename = calc_package_filename(ppackage,v,parch);
		if( !*filename ) {
			free(pversion);free(parch);
			free(ppackage);
			IFREE(origfilename);
			IFREE(md5andsize);
			IFREE(sourcename);
			return -1;
		}
		free(pversion);free(parch);
	}

	if( packagename == NULL)
		free(ppackage);

	return 1;
}

/* check if one chunk describes a packages superseded by another
 * return 1=new is better, 0=old is better, <0 error */
static int binaries_isnewer(const char *newchunk,const char *oldchunk) {
	char *nv,*ov;
	int r;

	/* if new broken, old is better, if old broken, new is better: */
	nv = chunk_dupvalue(chunk_getfield("Version",newchunk));
	if( !nv )
		return -1;
	ov = chunk_dupvalue(chunk_getfield("Version",oldchunk));
	if( !ov ) {
		free(nv);
		return 1;
	}
	r = isVersionNewer(nv,ov);
	free(nv);free(ov);
	return r;
}

/* call action for each package in packages_file */
int binaries_add(DB *pkgs,const char *part,const char *packages_file, binary_package_action action,void *data) {
	gzFile *fi;
	char *chunk,*oldchunk;
	char *package,*filename,*oldfile,*sourcename,*filekey,*md5andsize;
	int r,hadold=0;

	fi = gzopen(packages_file,"r");
	if( !fi ) {
		fprintf(stderr,"Unable to open file %s\n",packages_file);
		return -1;
	}
	while( (chunk = chunk_read(fi))) {
		if( binaries_parse_chunk(chunk,&package,&oldfile,&sourcename,&filename,&md5andsize) > 0) {
			hadold = 0;
			oldchunk = getpackage(pkgs,package);
			if( oldchunk && (r=binaries_isnewer(chunk,oldchunk)) != 0 ) {
				if( r < 0 ) {
					fprintf(stderr,"Omitting %s because of parse errors.\n",package);
					goto err;
				}
				/* old package will be obsoleted */
				hadold=1;
				free(oldchunk);
				oldchunk = NULL;
			}
			if( oldchunk == NULL ) {
				/* add package (or whatever action wants to do) */

				filekey =  calc_filekey(part,sourcename,filename);
				if( !filekey )
					goto err;

				r = (action)(data,chunk,package,sourcename,oldfile,filename,filekey,md5andsize,hadold);
				free(filekey);

				if( (r < 0 && !force) || r< -1 )
					goto err;

			} else
				free(oldchunk);
			
			free(package);free(md5andsize);
			free(oldfile);free(filename);free(sourcename);
		} else {
			fprintf(stderr,"Cannot parse chunk: '%s'!\n",chunk);
		}
		free(chunk);
	}
	gzclose(fi);
	return 0;
err:
	free(md5andsize);free(oldfile);free(package);
	free(sourcename);free(filename);
	gzclose(fi);
	return -1;

}
