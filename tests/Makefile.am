
EXTRA_DIST = test.sh genpackage.sh dpkg-architecture testchunk.c

MAINTAINERCLEANFILES = Makefile.in
CLEANFILES = testchunk.data.gz testchunk.data test testchunk2.data.gz testchunk2.data

AM_CPPFLAGS = -Wall

noinst_PROGRAMS = randomgen randomgen2
randomgen_SOURCES = randomgen.c
randomgen2_SOURCES = randomgen2.c

testchunk: testchunk.c ../ignore.o ../strlist.o ../chunks.o
	gcc -o testchunk -O2 -Wall -g testchunk.c ../ignore.o ../strlist.o ../chunks.o -lz

%.gz: %
	gzip -c $< > $@

testchunk.data: randomgen
	./randomgen 4000000 1 /var/lib/apt/lists/*_Packages > testchunk.data

testchunk2.data: randomgen2
	./randomgen2 4000000 1 > testchunk2.data

VALGRIND = valgrind -v --trace-children=no --single-step=yes --num-callers=30 --show-reachable=yes --leak-check=yes

test: testchunk testchunk.data.gz
	$(VALGRIND) ./testchunk testchunk.data.gz

test2: testchunk testchunk2.data.gz
	$(VALGRIND) ./testchunk testchunk2.data.gz
