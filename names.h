#ifndef __MIRRORER_NAMES_H
#define __MIRRORER_NAMES_H

char *calc_dirconcat(const char *str1,const char *str2);
char *calc_package_filename(const char *package,const char *version,const char *arch);
char *calc_sourcedir(const char *part,const char *sourcename);
char *calc_filekey(const char *part,const char *sourcename,const char *filename);
char *calc_srcfilekey(const char *sourcedir,const char *filename);
char *calc_fullfilename(const char *mirrordir,const char *filekey);
char *calc_fullsrcfilename(const char *mirrordir,const char *directory,const char *filename);

#endif
