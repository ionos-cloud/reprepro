#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef unsigned char uchar;

const char values[] = "aaaaaaaaa: \t\r\n\n";

static inline uchar getrandom(void) {
	long r = random() % sizeof(values);
	return values[r];
}

int main(int argc, const char *argv[]) {
	int size;

	if( argc != 3 ) {
		fprintf(stderr,"Syntax: randomgen <size> <randomseed>\n");
		return EXIT_FAILURE;
	}
	size = atoi(argv[1]);
	srandom(atoi(argv[2]));

	while( size-- > 0 ) {
		unsigned char c;

		c = getrandom();
		write(1,&c,1);
	}
	return EXIT_SUCCESS;
}
