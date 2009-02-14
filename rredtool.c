#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include "globals.h"
#include "error.h"
#include "rredpatch.h"

static const struct option options[] = {
	{"version", no_argument, NULL, 'V'},
	{"help", no_argument, NULL, 'h'},
	{"debug", no_argument, NULL, 'D'},
	{"merge", no_argument, NULL, 'm'},
	{NULL, 0, NULL, 0}
};

int main(int argc, const char *argv[]) {
	struct rred_patch *patches[argc];
	struct modification *m;
	retvalue r;
	bool mergemode = false;
	int i, count;
	const char *sourcename;
	int debug = 0;

	while( (i = getopt_long(argc, (char**)argv, "+hVDm", options, NULL)) != -1 ) {
		switch (i) {
			case 'h':
				puts(
"rred-tool: handle some subset of ed-patches\n"
"Syntax: rred-tool --merge patches...\n"
"or: rred-tool file-to-patch patches...");
				return EXIT_SUCCESS;
			case 'V':
				printf("rred-tool from " PACKAGE_NAME " version " PACKAGE_VERSION);
				return EXIT_SUCCESS;
			case 'D':
				debug++;
				break;
			case 'm':
				mergemode = 1;
				break;
			case '?':
			default:
				return EXIT_FAILURE;

		}
	}

	i = optind;
	if( !mergemode ) {
		if( i >= argc ) {
			fprintf(stderr, "Not enough arguments!\n");
			return EXIT_FAILURE;
		}
		sourcename = argv[i++];
	}

	count = 0;
	while( i < argc ) {
		r = patch_load(argv[i], -1, &patches[count]);
		if( RET_IS_OK(r) )
			count++;
		if( RET_WAS_ERROR(r) ) {
			fprintf(stderr, "Aborting...\n");
			return RET_ERRNO(r);
		}
		i++;
	}
	if( count <= 0 ) {
		fprintf(stderr, "Not enough patches for operation...\n");
		return EXIT_FAILURE;
	}
	m = patch_getmodifications(patches[0]);
	for( i = 1; i < count ; i++ ) {
		struct modification *a = patch_getmodifications(patches[i]);
		if( debug ) {
			puts("--------RESULT SO FAR----------------");
			modification_printaspatch(m);
			puts("--------TO BE MERGED WITH------------");
			modification_printaspatch(a);
			puts("-------------END---------------------");
		}
		r = combine_patches(&m, m, a);
		if( RET_WAS_ERROR(r) ) {
			for( i = 0 ; i < count ; i++ ) {
				patch_free(patches[i]);
			}
			fprintf(stderr, "Aborting...\n");
			return RET_ERRNO(r);
		}
	}
	if( mergemode )
		modification_printaspatch(m);
	modification_freelist(m);
	for( i = 0 ; i < count ; i++ )
		patch_free(patches[i]);
	return EXIT_SUCCESS;
}

