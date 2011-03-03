#ifndef REPREPRO_DIFFINDEX_H
#define REPREPRO_DIFFINDEX_H

struct diffindex {
	struct checksums *destination;
	int patchcount;
	struct diffindex_patch {
		struct checksums *frompackages;
		char *name;
		struct checksums *checksums;
		/* safe-guard against cycles */
		bool done;
	} patches[];
};

void diffindex_free(/*@only@*/struct diffindex *);
retvalue diffindex_read(const char *, /*@out@*/struct diffindex **);

#endif
