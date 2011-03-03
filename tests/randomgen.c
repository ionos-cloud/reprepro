#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// typedef unsigned long ulong;
typedef unsigned char uchar;

ulong allcount;
ulong counts[256];
ulong nextcount[256*256];
ulong next[256*256*256];

static inline uchar randomchar() {
	uchar c = '\0';
	ulong r = ((ulong)random())%(256+allcount);

	while( r > counts[c] && c < 0xFF ) {
		r-= counts[c];
		r--;
		c++;
	}
//	if( c == 0xFF )
//		fprintf(stderr,"left %ld %ld\n",counts[0xFF],r);
	return c;

}

static inline uchar randomchar2(int ofs) {
	uchar c = '\0';
	ulong r = ((ulong)random())%(256+nextcount[ofs]);

	ofs <<= 8;

	while( r > next[ofs+c] ) {
		c++;
		r-= next[ofs+c];
		r--;
	}
	return c;

}

static void generate(int size) {
	uchar c[3];

	c[0] = randomchar();
	putchar(c[0]);
	c[1] = randomchar();
	putchar(c[1]);
	size--; size--;
	while( size > 0 ) {
		if( (random() & 7) == 0 ) {
			c[2] = randomchar();
		} else {
			c[2] = randomchar((((int)c[0])<<8)+c[1]);
		}
		putchar(c[2]);
		c[0] = c[1];
		c[1] = c[2];
		size--;
	}
}

int main(int argc, const char *argv[]) {
	int i,fd;
	int size;

	if( argc < 4 ) {
		fprintf(stderr,"Syntax: randomgen <size> <randomseed> <sample-files>\n");
		return EXIT_FAILURE;
	}
	allcount = 0;
	size = atoi(argv[1]);
	srandom(atoi(argv[2]));
	fprintf(stderr,"Initializing data relationships...\n");

	for( i = 0 ; i < 256 ; i ++ )
		counts[i] = 0;
	for( i = 0 ; i < 256*256 ; i ++ )
		nextcount[i] = 0;
	for( i = 0 ; i < 256*256*256 ; i ++ )
		next[i] = 0;

	for( i = 3 ; i <argc ; i++ ) {
		uchar c[3];
		ssize_t r;

		fprintf(stderr,"Reading '%s'\n",argv[i]);
		fd = open(argv[i],O_RDONLY);
		if( fd < 0 ) {
			fprintf(stderr,"Error opening '%s' for reading: %m\n",argv[i]);
			return EXIT_FAILURE;
		}
		r = read(fd,&c,3);
		if( r < 0 ) {
			fprintf(stderr,"Error reading from '%s': %m\n",argv[i]);
			return EXIT_FAILURE;
		}
		if( r == 0 ) {
			fprintf(stderr,"Warning: ignoring emtpy file :'%s'\n",argv[i]);
		} else if( r < 3 ) {
			counts[c[0]] += 5000;
			allcount+= 5000;
		} else {
			int o;

			counts[c[0]]++;
			counts[c[1]]++;
			allcount+=2;
			do {
				allcount++;
				if( allcount >= ULONG_MAX-260 ) {
					fprintf(stderr,"Too much sample data. Sorry!\n");
					return EXIT_FAILURE;

				}
				counts[c[2]]++;
				o = (((int)c[0])<<8)+c[1];
				nextcount[o]++;
				next[(o<<8)+c[2]]++;
				c[0] = c[1];
				c[1] = c[2];
			} while( (r = read(fd,&c[2],1)) == 1);
			if( r < 0 ) {
				fprintf(stderr,"Error reading from '%s': %m\n",argv[i]);
				return EXIT_FAILURE;
			}
		}
		(void)close(fd);

	}
	fprintf(stderr,"Generating output...\n");
	generate(size);
	return EXIT_SUCCESS;
}
