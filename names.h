#ifndef REPREPRO_NAMES_H
#define REPREPRO_NAMES_H

char *calc_addsuffix(const char *str1,const char *str2);
char *calc_dirconcat(const char *str1,const char *str2);
char *calc_dirconcat3(const char *str1,const char *str2,const char *str3);

typedef enum {ic_uncompressed=0, ic_gzip } indexcompression;
#define ic_max ic_gzip

char *calc_comprconcat(const char *str1,const char *str2,const char *str3,indexcompression compr);

char *calc_binary_basename(const char *package,const char *version,const char *architecture,const char *packagetype);
char *calc_source_basename(const char *name,const char *version);
char *calc_sourcedir(const char *component,const char *sourcename);
char *calc_filekey(const char *component,const char *sourcename,const char *filename);
char *calc_srcfilekey(const char *sourcedir,const char *filename);
char *calc_fullfilename(const char *mirrordir,const char *filekey);
char *calc_fullsrcfilename(const char *mirrordir,const char *directory,const char *filename);
char *calc_identifier(const char *codename,const char *component,const char *architecture,const char *packagetype);
char *calc_concatmd5andsize(const char *md5sum,const char *size);
char *names_concatmd5sumandsize(const char *md5start,const char *md5end,const char *sizestart,const char *sizeend);

char *calc_downloadedlistfile(const char *listdir,const char *codename,const char *origin,const char *component,const char *architecture,const char *packagetype);
char *calc_downloadedlistpattern(const char *codename);

/* Create a strlist consisting out of calc_dirconcat'ed entries of the old */
retvalue calc_dirconcats(const char *directory, const struct strlist *basefilenames,struct strlist *files);

/* split a "<md5> <size> <filename>" into md5sum and filename */
retvalue calc_parsefileline(const char *fileline,char **filename,char **md5sum);

/* move over a version number, if epochsuppresed is true, colons may happen even without epoch there */
void names_overversion(const char **version,bool_t epochsuppressed);

/* check for forbidden characters */
retvalue propersourcename(const char *string);
retvalue properfilenamepart(const char *string);
retvalue properfilename(const char *string);
retvalue properfilenames(const struct strlist *names);
retvalue properpackagename(const char *string);
retvalue properversion(const char *string);
retvalue properidentifierpart(const char *string);

#endif
