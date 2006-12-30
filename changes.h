#ifndef REPREPRO_CHANGES_H
#define REPREPRO_CHANGES_H

typedef	enum { fe_UNKNOWN=0,fe_DEB,fe_UDEB,fe_DSC,fe_DIFF,fe_ORIG,fe_TAR} filetype;

#define FE_BINARY(ft) ( (ft) == fe_DEB || (ft) == fe_UDEB )
#define FE_SOURCE(ft) ( (ft) == fe_DIFF || (ft) == fe_ORIG || (ft) == fe_TAR || (ft) == fe_DSC || (ft) == fe_UNKNOWN)

retvalue changes_parsefileline(const char *fileline, filetype *result_type,
		char **result_basename, char **result_md5sum,
		char **result_section, char **result_priority,
		char **result_architecture, char **result_name);
#endif
