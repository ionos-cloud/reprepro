#include <config.h>

#include <stdio.h>
#include <stdarg.h>

#include "mprintf.h"

// TODO: check for asprintf in configure and
// write a replacement for such situations.

char * mprintf(const char *fmt,...) {
	char *p;
	int r;
	va_list va;

	va_start(va,fmt);
	r = vasprintf(&p,fmt,va);
	va_end(va);
	if( r < 0 )
		return NULL;
	else
		return p;
}

char * vmprintf(const char *fmt,va_list va) {
	char *p;
	int r;

	r = vasprintf(&p,fmt,va);
	if( r < 0 )
		return NULL;
	else
		return p;
}
