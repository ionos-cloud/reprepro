#ifndef REPREPRO_NAMES_H
#define REPREPRO_NAMES_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

char *calc_addsuffix(const char *str1,const char *str2);
char *calc_dirconcat(const char *str1,const char *str2);
char *calc_dirconcatn(const char *str1,const char *str2,size_t len2);
char *calc_dirsuffixconcat(const char *str1,const char *str2,const char *suffix);
char *calc_dirconcat3(const char *str1,const char *str2,const char *str3);


char *calc_binary_basename(const char *package,const char *version,const char *architecture,const char *packagetype);
char *calc_source_basename(const char *name,const char *version);
char *calc_changes_basename(const char *name,const char *version,const struct strlist *architectures);
char *calc_sourcedir(const char *component,const char *sourcename);
char *calc_filekey(const char *component,const char *sourcename,const char *filename);
char *calc_fullfilename(const char *mirrordir,const char *filekey);
char *calc_fullsrcfilename(const char *mirrordir,const char *directory,const char *filename);
char *calc_identifier(const char *codename,const char *component,const char *architecture,const char *packagetype);
char *calc_concatmd5andsize(const char *md5sum,const char *size);
char *names_concatmd5sumandsize(const char *md5start,const char *md5end,const char *sizestart,const char *sizeend);
retvalue calc_extractsize(const char *checksum, /*@out@*/off_t *size);
char *calc_trackreferee(const char *codename,const char *sourcename,const char *sourceversion);

char *calc_downloadedlistfile(const char *listdir,const char *codename,const char *origin,const char *component,const char *architecture,const char *packagetype);
char *calc_downloadedlistpattern(const char *codename);

/* Create a strlist consisting out of calc_dirconcat'ed entries of the old */
retvalue calc_dirconcats(const char *directory, const struct strlist *basefilenames,/*@out@*/struct strlist *files);

/* split a "<md5> <size> <filename>" into md5sum and filename */
retvalue calc_parsefileline(const char *fileline,/*@out@*/char **filename,/*@out@*//*@null@*/char **md5sum);

/* move over a version number, if epochsuppresed is true, colons may happen even without epoch there */
void names_overversion(const char **version,bool_t epochsuppressed);

/* check for forbidden characters */
retvalue propersourcename(const char *string);
retvalue properfilenamepart(const char *string);
retvalue properfilename(const char *string);
retvalue properfilenames(const struct strlist *names);
retvalue properpackagename(const char *string);
retvalue properversion(const char *string);

static inline bool_t endswith(const char *name, const char *suffix) {
	size_t ln,ls;
	ln = strlen(name);
	ls = strlen(suffix);
	return ln > ls && strcmp(name+(ln-ls),suffix) == 0;
}

#endif
