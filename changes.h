#ifndef REPREPRO_CHANGES_H
#define REPREPRO_CHANGES_H

#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif

typedef enum {
	fe_UNKNOWN=0,
	fe_DEB, fe_UDEB, fe_DDEB,
	fe_DSC, fe_DIFF, fe_ORIG, fe_TAR,
	fe_SIG,
	fe_ALTSRC,
	fe_BYHAND, fe_LOG, fe_CHANGES,
	fe_BUILDINFO
} filetype;

#define FE_PACKAGE(ft) ((ft) == fe_DEB || (ft) == fe_UDEB || (ft) == fe_DSC || (ft) == fe_DDEB)
#define FE_BINARY(ft) ((ft) == fe_DEB || (ft) == fe_DDEB || (ft) == fe_UDEB)
#define FE_SOURCE(ft) ((ft) == fe_DIFF || (ft) == fe_ORIG || (ft) == fe_TAR || (ft) == fe_DSC || (ft) == fe_UNKNOWN || (ft) == fe_ALTSRC || (ft) == fe_SIG)

struct hash_data;
retvalue changes_parsefileline(const char * /*fileline*/, /*@out@*/filetype *, /*@out@*/char ** /*result_basename*/, /*@out@*/struct hash_data *, /*@out@*/struct hash_data *, /*@out@*/char ** /*result_section*/, /*@out@*/char ** /*result_priority*/, /*@out@*/architecture_t *, /*@out@*/char ** /*result_name*/);
#endif
