#ifndef __MIRRORER_NAMES_H
#define __MIRRORER_NAMES_H

char *calc_addsuffix(const char *str1,const char *str2);
char *calc_dirconcat(const char *str1,const char *str2);
char *calc_binary_basename(const char *package,const char *version,const char *architecture);
char *calc_source_basename(const char *name,const char *version);
char *calc_sourcedir(const char *component,const char *sourcename);
char *calc_filekey(const char *component,const char *sourcename,const char *filename);
char *calc_srcfilekey(const char *sourcedir,const char *filename);
char *calc_fullfilename(const char *mirrordir,const char *filekey);
char *calc_fullsrcfilename(const char *mirrordir,const char *directory,const char *filename);
char *calc_identifier(const char *codename,const char *component,const char *architecture);
char *calc_concatmd5andsize(const char *md5sum,const char *size);

/* Create a strlist consisting out of calc_dirconcat'ed entries of the old */
retvalue calc_dirconcats(const char *directory, const struct strlist *basefilenames,struct strlist *files);

#endif
