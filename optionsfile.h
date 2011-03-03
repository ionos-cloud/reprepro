#ifndef REPREPRO_OPTIONSFILE_H
#define REPREPRO_OPTIONSFILE_H

#include <getopt.h>

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

void optionsfile_parse(const struct option *longopts, void handle_option(int,const char *));

#endif /*REPREPRO_OPTIONSFILE_H*/
