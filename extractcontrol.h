#ifndef __MIRRORER_EXTRACTCONTROL_H
#define __MIRRORER_EXTRACTCONTROL_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

/* Read the control information of <debfile< and save it's only chunk
 * into <control> */

retvalue extractcontrol(char **control,const char *debfile);


#endif
