#ifndef REPREPRO_COPYPACKAGES_H
#define REPREPRO_COPYPACKAGES_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

struct nameandversion {
	const char *name;
	const char /*@null@*/ *version;
	bool warnedabout;
	bool found;
};

retvalue copy_by_name(struct distribution * /*into*/, struct distribution * /*from*/, struct nameandversion *, const struct atomlist *, const struct atomlist *, const struct atomlist *, bool);
retvalue copy_by_source(struct distribution * /*into*/, struct distribution * /*from*/, int, const char **, const struct atomlist *, const struct atomlist *, const struct atomlist *, bool);
retvalue copy_by_formula(struct distribution * /*into*/, struct distribution * /*from*/, const char *formula, const struct atomlist *, const struct atomlist *, const struct atomlist *, bool);
retvalue copy_by_glob(struct distribution * /*into*/, struct distribution * /*from*/, const char * /*glob*/, const struct atomlist *, const struct atomlist *, const struct atomlist *, bool);

retvalue copy_from_file(struct distribution * /*into*/, component_t, architecture_t, packagetype_t, const char * /*filename*/ , int, const char **);

/* note that snapshotname must live till logger_wait has run */
retvalue restore_by_name(struct distribution *, const struct atomlist *, const struct atomlist *, const struct atomlist *, const char * /*snapshotname*/, int, const char **);
retvalue restore_by_source(struct distribution *, const struct atomlist *, const struct atomlist *, const struct atomlist *, const char * /*snapshotname*/, int, const char **);
retvalue restore_by_formula(struct distribution *, const struct atomlist *, const struct atomlist *, const struct atomlist *, const char * /*snapshotname*/, const char *filter);
retvalue restore_by_glob(struct distribution *, const struct atomlist *, const struct atomlist *, const struct atomlist *, const char * /*snapshotname*/, const char * /*glob*/);

#endif
