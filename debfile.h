#ifndef REPREPRO_DEBFILE_H
#define REPREPRO_DEBFILE_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* Read the control information of <debfile> and put it's only chunk
 * into <control> */
retvalue extractcontrol(/*@out@*/char **control,const char *debfile);

/* Read a list of files of <debfile> */
retvalue getfilelist(/*@out@*/char **filelist, /*@out@*/ size_t *size, const char *debfile);

#endif
