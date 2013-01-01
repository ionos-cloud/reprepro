#include <config.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "mprintf.h"

// TODO: check for asprintf in configure and
// write a replacement for such situations.

char * mprintf(const char *fmt, ...) {
	char *p;
	int r;
	va_list va;

	va_start(va, fmt);
	r = vasprintf(&p, fmt, va);
	va_end(va);
	/* return NULL both when r is < 0 and when NULL was returned */
	if (r < 0)
		return NULL;
	else
		return p;
}

char * vmprintf(const char *fmt, va_list va) {
	char *p;
	int r;

	r = vasprintf(&p, fmt, va);
	/* return NULL both when r is < 0 and when NULL was returned */
	if (r < 0)
		return NULL;
	else
		return p;
}

#ifndef HAVE_DPRINTF
int dprintf(int fd, const char *format, ...){
	char *buffer;
	int ret;

	va_list va;

	va_start(va, format);
	buffer = vmprintf(format, va);
	va_end(va);
	if (buffer == NULL)
		return -1;
	ret = write(fd, buffer, strlen(buffer));
	free(buffer);
	return ret;
}
#endif

#ifndef HAVE_STRNDUP
/* That's not the best possible strndup implementation, but it suffices for what
 * it is used here */
char *strndup(const char *str, size_t n) {
	char *r = malloc(n+1);
	if (r == NULL)
		return r;
	memcpy(r, str, n);
	r[n] = '\0';
	return r;
}
#endif
