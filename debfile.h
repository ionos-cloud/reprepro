#ifndef REPREPRO_DEBFILE_H
#define REPREPRO_DEBFILE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* Read the control information of <debfile> and save it's only chunk
 * into <control> */

retvalue extractcontrol(/*@out@*/char **control,const char *debfile);

#ifdef HAVE_LIBARCHIVE
/* The following are only in debfile.c and not in extractcontrol.c,
 * thus only available when compiled with libarchive */

retvalue getfilelist(/*@out@*/char **filelist, const char *debfile);

#endif

#endif
