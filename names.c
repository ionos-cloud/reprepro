#include <assert.h>
#include <stdio.h>
#include "names.h"

char *calc_srcfilekey(const char *sourcedir,const char *filename){
	char *p;
	asprintf(&p,"%s/%s",sourcedir,filename);
	return p;
}

char *calc_fullfilename(const char *mirrordir,const char *filekey){
	char *p;
	asprintf(&p,"%s/%s",mirrordir,filekey);
	return p;
}

char *calc_fullsrcfilename(const char *mirrordir,const char *directory,const char *filename){
	char *p;
	asprintf(&p,"%s/%s/%s",mirrordir,directory,filename);
	return p;
}

char *calc_sourcedir(const char *part,const char *sourcename) {
	char *p;

	assert( *sourcename != '\0' );

	if( sourcename[0] == 'l' && sourcename[1] == 'i' && sourcename[2] == 'b' && sourcename[3] != '\0' )

		asprintf(&p,"%s/lib%c/%s",part,sourcename[3],sourcename);
	else if( *sourcename != '\0' )
		asprintf(&p,"%s/%c/%s",part,sourcename[0],sourcename);
	else
		return NULL;
	return p;
}

char *calc_filekey(const char *part,const char *sourcename,const char *filename) {
	char *p;
	if( sourcename[0] == 'l' && sourcename[1] == 'i' && sourcename[2] == 'b' && sourcename[3] != '\0' )

		asprintf(&p,"%s/lib%c/%s/%s",part,sourcename[3],sourcename,filename);
	else if( *sourcename != '\0' )
		asprintf(&p,"%s/%c/%s/%s",part,sourcename[0],sourcename,filename);
	else
		return NULL;
	return p;
}


char *calc_package_filename(const char *name,const char *version,const char *arch) {
	char *filename;
	
	asprintf(&filename,"%s_%s_%s.deb",name,version,arch);
	return filename;
}
