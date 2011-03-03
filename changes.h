#ifndef REPREPRO_CHANGES_H
#define REPREPRO_CHANGES_H

typedef	enum { fe_UNKNOWN=0,fe_DEB,fe_UDEB,fe_DSC,fe_DIFF,fe_ORIG,fe_TAR} filetype;

#define FE_PACKAGE(ft) ( (ft) == fe_DEB || (ft) == fe_UDEB || (ft) == fe_DSC )
#define FE_BINARY(ft) ( (ft) == fe_DEB || (ft) == fe_UDEB )
#define FE_SOURCE(ft) ( (ft) == fe_DIFF || (ft) == fe_ORIG || (ft) == fe_TAR || (ft) == fe_DSC || (ft) == fe_UNKNOWN)

struct hash_data;
retvalue changes_parsefileline(const char *fileline, /*@out@*/filetype *result_type, /*@out@*/char **result_basename, /*@out@*/struct hash_data *, /*@out@*/struct hash_data *, /*@out@*/char **result_section, /*@out@*/char **result_priority, /*@out@*/char **result_architecture, /*@out@*/char **result_name);
#endif
