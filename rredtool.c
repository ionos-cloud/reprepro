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
	{"patch", no_argument, NULL, 'p'},
	{NULL, 0, NULL, 0}
};

int main(int argc, const char *argv[]) {
	struct rred_patch *patches[argc];
	struct modification *m;
	retvalue r;
	bool mergemode = false;
	bool patchmode = false;
	int i, count;
	const char *sourcename IFSTUPIDCC(=NULL);
	int debug = 0;

	while( (i = getopt_long(argc, (char**)argv, "+hVDmp", options, NULL)) != -1 ) {
		switch (i) {
			case 'h':
				puts(
"rred-tool: handle some subset of ed-patches\n"
"Syntax: rred-tool --merge patches...\n"
"or: rred-tool --patch file-to-patch patches...");
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
			case 'p':
				patchmode = 1;
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
	if( mergemode && patchmode ) {
		fprintf(stderr, "Cannot do --merge and --patch at the same time!\n");
		return EXIT_FAILURE;
	}
	if( !mergemode && !patchmode ) {
		fprintf(stderr, "Need either --merge or --patch!\n");
		return EXIT_FAILURE;
	}

	count = 0;
	while( i < argc ) {
		r = patch_load(argv[i], -1, &patches[count]);
		if( RET_IS_OK(r) )
			count++;
		if( RET_WAS_ERROR(r) ) {
			if( r == RET_ERROR_OOM )
				fputs("Out of memory!\n", stderr);
			else
				fputs("Aborting...\n", stderr);
			return EXIT_FAILURE;
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
			fputs("--------RESULT SO FAR--------\n", stderr);
			modification_printaspatch(stderr, m);
			fputs("--------TO BE MERGED WITH-----\n", stderr);
			modification_printaspatch(stderr, a);
			fputs("-------------END--------------\n", stderr);
		}
		r = combine_patches(&m, m, a);
		if( RET_WAS_ERROR(r) ) {
			for( i = 0 ; i < count ; i++ ) {
				patch_free(patches[i]);
			}
			if( r == RET_ERROR_OOM )
				fputs("Out of memory!\n", stderr);
			else
				fputs("Aborting...\n", stderr);
			return EXIT_FAILURE;
		}
	}
	r = RET_OK;
	if( mergemode ) {
		modification_printaspatch(stdout, m);
	} else {
		r = patch_file(stdout, sourcename, m);
	}
	if( ferror(stdout) ) {
		fputs("Error writing to stdout!\n", stderr);
		r = RET_ERROR;
	}
	modification_freelist(m);
	for( i = 0 ; i < count ; i++ )
		patch_free(patches[i]);
	if( r == RET_ERROR_OOM )
		fputs("Out of memory!\n", stderr);
	if( RET_WAS_ERROR(r) )
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

