#ifndef __MIRRORER_NAMES_H
#define __MIRRORER_NAMES_H

char *calc_addsuffix(const char *str1,const char *str2);
char *calc_dirconcat(const char *str1,const char *str2);
char *calc_dirconcat3(const char *str1,const char *str2,const char *str3);

typedef enum {ic_uncompressed=0, ic_gzip } indexcompression;
#define ic_max ic_gzip

char *calc_comprconcat(const char *str1,const char *str2,const char *str3,indexcompression compr);

char *calc_binary_basename(const char *package,const char *version,const char *architecture);
char *calc_source_basename(const char *name,const char *version);
char *calc_sourcedir(const char *component,const char *sourcename);
char *calc_filekey(const char *component,const char *sourcename,const char *filename);
char *calc_srcfilekey(const char *sourcedir,const char *filename);
char *calc_fullfilename(const char *mirrordir,const char *filekey);
char *calc_fullsrcfilename(const char *mirrordir,const char *directory,const char *filename);
char *calc_identifier(const char *codename,const char *component,const char *architecture);
char *calc_concatmd5andsize(const char *md5sum,const char *size);
char *names_concatmd5sumandsize(const char *md5start,const char *md5end,const char *sizestart,const char *sizeend);

char *calc_downloadedlistfile(const char *listdir,const char *codename,const char *origin,const char *component,const char *architecture);

/* Create a strlist consisting out of calc_dirconcat'ed entries of the old */
retvalue calc_dirconcats(const char *directory, const struct strlist *basefilenames,struct strlist *files);

/* move over a valid name of a package. (Who allowed "source(vers)" ??) */
void names_overpkgname(const char **name_end);
/* move over a version number */
void names_overversion(const char **version);
/* check for a string to be a valid package name */
retvalue names_checkpkgname(const char *name);
/* check for a string to be a valid version number */
retvalue names_checkversion(const char *version);
/* check for a string to be a valid filename */
retvalue names_checkbasename(const char *basename);

#endif
