#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "dirs.h"

extern int verbose;

/* create directory dirname. returns 0 on success or if already existing, -1 otherwise */
static int check_dir(const char *dirname) {
	int ret;

	ret = mkdir(dirname,0770);
	if( ret == 0 ) {
		if( verbose )
		fprintf(stderr,"Created directory \"%s\"\n",dirname);
		return 0;
	} else if( ret < 0 && errno != EEXIST ) {
		fprintf(stderr,"Can not create directory \"%s\": %m\n",dirname);
		return -1;
	}
	return 0;
}

/* create recursively all parent directories before the last '/' */
int make_parent_dirs(const char *filename) {
	const char *p;
	char *h;
	int i;

	for( p = filename+1, i = 1 ; *p ; p++,i++) {
		if( *p == '/' ) {
			h = strndup(filename,i);
			if( check_dir(h) < 0 ) {
				free(h);
				return -1;
			}
			free(h);
		}
	}
	return 0;
}

/* create dirname and any '/'-seperated part of it */
int make_dir_recursive(const char *dirname) {
	make_parent_dirs(dirname);
	return check_dir(dirname);
}
