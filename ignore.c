#include <config.h>

#include <stdio.h>
#include <string.h>

#include "ignore.h"

int ignored[IGN_COUNT];
bool_t ignore[IGN_COUNT];

void init_ignores(void) {
	int i;
	for( i = 0 ; i < IGN_COUNT ; i++ ) {
		ignored[i] = 0;
		ignore[i] = FALSE;
	}
}

retvalue add_ignore(const char *given) {
	int i;
	static const char *ignores[] = {
#define IGN(what) #what ,
	VALID_IGNORES
#undef IGN
	};

	//TODO: allow multiple values sperated by some sign here...

	for( i = 0 ; i < IGN_COUNT ; i++) {
		if( strcmp(given,ignores[i]) == 0 ) {
			ignore[i] = TRUE;
			break;
		}
	}
	if( i == IGN_COUNT ) {
		if( IGNORING("Ignoring","To Ignore",ignore,"Unknown --ignore value: '%s'!\n",given))
			return RET_NOTHING;
		else
			return RET_ERROR;
	} else
		return RET_OK;
}
