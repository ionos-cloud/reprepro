#ifndef REPREPRO_TERMDECIDE_H
#define REPREPRO_TERMDECIDE_H

#ifndef REPREPRO_TERMS_H
#include "terms.h"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif
#ifndef REPREPRO_PACKAGE_H
#include "package.h"
#endif

retvalue term_compilefortargetdecision(/*@out@*/term **, const char *);
retvalue term_decidepackage(const term *, struct package *, struct target *);



#endif
