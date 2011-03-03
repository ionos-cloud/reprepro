#ifndef REPREPRO_NAMES_H
#define REPREPRO_NAMES_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

char *calc_addsuffix(const char *str1,const char *str2);
char *calc_dirconcat(const char *str1,const char *str2);
char *calc_dirconcat3(const char *str1,const char *str2,const char *str3);

#define calc_conffile(name) calc_dirconcat(global.confdir, name)
char *calc_changes_basename(const char *name,const char *version,const struct strlist *architectures);
char *calc_trackreferee(const char *codename,const char *sourcename,const char *sourceversion);
#define calc_snapshotbasedir(codename, name) mprintf("%s/%s/snapshots/%s", global.distdir, codename, name)


/* Create a strlist consisting out of calc_dirconcat'ed entries of the old */
retvalue calc_dirconcats(const char *directory, const struct strlist *basefilenames,/*@out@*/struct strlist *files);
retvalue calc_inplacedirconcats(const char *directory, struct strlist *);

/* move over a version number, if epochsuppresed is true, colons may happen even without epoch there */
void names_overversion(const char **version, bool epochsuppressed);

/* check for forbidden characters */
retvalue propersourcename(const char *string);
retvalue properfilenamepart(const char *string);
retvalue properfilename(const char *string);
retvalue properfilenames(const struct strlist *names);
retvalue properpackagename(const char *string);
retvalue properversion(const char *string);

static inline bool endswith(const char *name, const char *suffix) {
	size_t ln,ls;
	ln = strlen(name);
	ls = strlen(suffix);
	return ln > ls && strcmp(name+(ln-ls),suffix) == 0;
}

#endif
