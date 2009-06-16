#ifndef REPREPRO_COPYPACKAGES_H
#define REPREPRO_COPYPACKAGES_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif

retvalue copy_by_name(struct database *, struct distribution *into, struct distribution *from, int, const char **, component_t, architecture_t, packagetype_t);
retvalue copy_by_source(struct database *, struct distribution *into, struct distribution *from, int, const char **, component_t, architecture_t, packagetype_t);
retvalue copy_by_formula(struct database *, struct distribution *into, struct distribution *from, const char *formula, component_t, architecture_t, packagetype_t);
retvalue copy_by_glob(struct database *, struct distribution *into, struct distribution *from, const char *glob, component_t, architecture_t, packagetype_t);

retvalue copy_from_file(struct database *, struct distribution *into, component_t, architecture_t, packagetype_t, const char *filename, int, const char **);

/* note that snapshotname must live till logger_wait has run */
retvalue restore_by_name(struct database *, struct distribution *, component_t, architecture_t, packagetype_t, const char *snapshotname, int, const char **);
retvalue restore_by_source(struct database *, struct distribution *, component_t, architecture_t, packagetype_t, const char *snapshotname, int, const char **);
retvalue restore_by_formula(struct database *, struct distribution *, component_t, architecture_t, packagetype_t, const char *snapshotname, const char *filter);
retvalue restore_by_glob(struct database *, struct distribution *, component_t, architecture_t, packagetype_t, const char *snapshotname, const char *glob);

#endif
