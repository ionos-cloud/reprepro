#ifndef REPREPRO_BYHANDHOOK_H
#define REPREPRO_BYHANDHOOK_H

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif

struct byhandhook;

retvalue byhandhooks_parse(struct configiterator *, /*@out@*/struct byhandhook **);

/* 2nd argument starts as NULL, returns true as long as there are hooks */
bool byhandhooks_matched(const struct byhandhook *, const struct byhandhook **, const char * /*section*/, const char * /*priority*/, const char * /*name*/);

retvalue byhandhook_call(const struct byhandhook *, const char * /*codename*/, const char * /*section*/, const char * /*priority*/, const char * /*basename*/, const char * /*fullfilename*/);

void byhandhooks_free(/*@null@*//*@only@*/struct byhandhook *);

#endif

