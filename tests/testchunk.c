#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>

#include "../error.h"
#include "../ignore.h"
#include "../strlist.h"
#include "../chunks.h"

int verbose = 30;

retvalue action(UNUSED(void *data),const char *chunk) {
	const char *c;
	char *lc,*p,*nc;
	struct fieldtoadd *f;

	lc = malloc(strlen(chunk)+5);
	assert( lc != NULL);
	p = lc;

	c = chunk;
	while( *c != '\0' ) {
		assert( *c!='\n' || *(c+1) != '\n' );
		*p = *c;
		c++;p++;
	}
	*(p++) = '\n';
	*(p++) = 'a';
	*(p++) = ':';
	*(p++) = '\n';
	*(p++) = '\0';
	f = addfield_new("aa","test",NULL);
	f = addfield_new("aaa","TEST",f);
	f = deletefield_new("a a",f);
	nc = chunk_replacefields(lc,f,"a");
	addfield_free(f);

	free(lc);
	c = nc;
	while( *c != '\0' ) {
		assert( *c!='\n' || *(c+1) != '\n' );
		c++;
	}
	free(nc);

	return RET_OK;
}


int main(int argc, char *argv[]) {
	retvalue r;

	if( argc != 2 ) {
		fprintf(stderr,"Syntax: testchunck <file>\n");
		return EXIT_FAILURE;
	}

	init_ignores();
	
	r = chunk_foreach(argv[1],action,NULL,TRUE,FALSE);
	if( RET_IS_OK(r) )
		return EXIT_SUCCESS;
	else
		return EXIT_FAILURE;
}
