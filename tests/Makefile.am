
EXTRA_DIST = test.sh genpackage.sh dpkg-architecture

MAINTAINERCLEANFILES = Makefile.in
CLEANFILES = testchunk.data.gz testchunk.data test

AM_CPPFLAGS = -D_GNU_SOURCE=1 -Wall

noinst_PROGRAMS = randomgen
randomgen_SOURCES = randomgen.c

testchunk: testchunk.c ../ignore.o ../strlist.o ../chunks.o
	gcc -o testchunk -O2 -Wall -g testchunk.c ../ignore.o ../strlist.o ../chunks.o -lz


testchunk.data.gz: testchunk.data
	gzip -c testchunk.data > testchunk.data.gz

testchunk.data: randomgen
	./randomgen 4000000 1 /var/lib/apt/lists/*_Packages > testchunk.data

VALGRIND = valgrind -v --trace-children=no --single-step=yes --num-callers=30 --show-reachable=yes --leak-check=yes
	
test: testchunk testchunk.data.gz
	$(VALGRIND) ./testchunk
