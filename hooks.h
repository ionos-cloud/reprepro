#ifndef REPREPRO_HOOKS_H
#define REPREPRO_HOOKS_H

#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif

/* the command currently processed (may not changed till all loggers are run) */
extern command_t causingcommand;
/* file causing the current actions (may change so may need to be saved for queued actions)*/
extern /*@null@*/ const char *causingfile;

/* for other hooks */
void sethookenvironment(/*@null@*/const char *, /*@null@*/const char *, /*@null@*/const char *, /*@null@*/const char *);

#endif
