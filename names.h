#ifndef REPREPRO_NAMES_H
#define REPREPRO_NAMES_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

char *calc_addsuffix(const char *, const char *);
char *calc_dirconcat(const char *, const char *);
char *calc_dirconcat3(const char *, const char *, const char *);

char *calc_changes_basename(const char *, const char *, const struct strlist *);
char *calc_trackreferee(const char *, const char *, const char *);
#define calc_snapshotbasedir(codename, name) mprintf("%s/%s/snapshots/%s", global.distdir, codename, name)


/* Create a strlist consisting out of calc_dirconcat'ed entries of the old */
retvalue calc_dirconcats(const char *, const struct strlist *, /*@out@*/struct strlist *);
retvalue calc_inplacedirconcats(const char *, struct strlist *);

/* move over a version number,
 * if epochsuppresed is true, colons may happen even without epoch there */
void names_overversion(const char **, bool /*epochsuppressed*/);

/* check for forbidden characters */
retvalue propersourcename(const char *);
retvalue properfilenamepart(const char *);
retvalue properfilename(const char *);
retvalue properfilenames(const struct strlist *);
retvalue properpackagename(const char *);
retvalue properversion(const char *);

static inline bool endswith(const char *name, const char *suffix) {
	size_t ln = strlen(name), ls = strlen(suffix);
	return ln > ls && strcmp(name + (ln - ls), suffix) == 0;
}

#endif
