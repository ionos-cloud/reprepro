#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "../error.h"
#include "../ignore.h"
#include "../strlist.h"
#include "../chunks.h"

int verbose = 30;

retvalue action(UNUSED(void *data),const char *chunk) {
	while( *chunk != '\0' ) {
		assert( *chunk!='\n' || *(chunk+1) != '\n' );
		chunk++;
	}
	return RET_OK;
}


int main() {
	retvalue r;

	init_ignores();
	
	r = chunk_foreach("testchunk.data.gz",action,NULL,TRUE,FALSE);
	if( RET_IS_OK(r) )
		return EXIT_SUCCESS;
	else
		return EXIT_FAILURE;
}
