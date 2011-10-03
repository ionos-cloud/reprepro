/*  written 2007 by Bernhard R. Link
 *  This file is in the public domain.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <config.h>

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include "filecntl.h"

#ifndef HAVE_CLOSEFROM
void closefrom(int lowfd) {
	long maxopen;
	int fd;

# ifdef F_CLOSEM
	if (fcntl(lowfd, F_CLOSEM, NULL) == 0)
		return;
# endif
	maxopen = sysconf(_SC_OPEN_MAX);
	if (maxopen > INT_MAX)
		maxopen = INT_MAX;
	if (maxopen < 0)
		maxopen = 1024;
	for (fd = lowfd ; fd <= maxopen ; fd++)
		(void)close(fd);
}
#endif

void markcloseonexec(int fd) {
	long l;
	l = fcntl(fd, F_GETFD, 0);
	if (l >= 0) {
		(void)fcntl(fd, F_SETFD, l|FD_CLOEXEC);
	}
}

int deletefile(const char *fullfilename) {
	int ret, e;

	ret = unlink(fullfilename);
	if (ret != 0) {
		e = errno;
		fprintf(stderr, "error %d unlinking %s: %s\n",
				e, fullfilename, strerror(e));
		return (e != 0)?e:EINVAL;
	}
	return 0;
}

bool isregularfile(const char *fullfilename) {
	struct stat s;
	int i;

	assert(fullfilename != NULL);
	i = stat(fullfilename, &s);
	return i == 0 && S_ISREG(s.st_mode);
}

bool isdirectory(const char *fullfilename) {
	struct stat s;
	int i;

	assert(fullfilename != NULL);
	i = stat(fullfilename, &s);
	return i == 0 && S_ISDIR(s.st_mode);
}

bool isanyfile(const char *fullfilename) {
	struct stat s;
	int i;

	assert(fullfilename != NULL);
	i = lstat(fullfilename, &s);
	return i == 0;
}
