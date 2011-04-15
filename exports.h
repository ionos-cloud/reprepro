#ifndef REPREPRO_EXPORTS_H
#define REPREPRO_EXPORTS_H

#ifndef REPREPRO_RELEASE_H
#include "release.h"
#endif

struct exportmode {
	/* "Packages", "Sources" or something like that */
	char *filename;
	/* create uncompressed, create .gz, <future things...> */
	compressionset compressions;
	/* Generate a Release file next to the Indexfile , if non-null*/
	/*@null@*/
	char *release;
	/* programms to start after all are generated */
	struct strlist hooks;
};

retvalue exportmode_init(/*@out@*/struct exportmode *, bool /*uncompressed*/, /*@null@*/const char * /*release*/, const char * /*indexfile*/);
struct configiterator;
retvalue exportmode_set(struct exportmode *, struct configiterator *);
void exportmode_done(struct exportmode *);

retvalue export_target(const char * /*relativedir*/, struct target *, const struct exportmode *, struct release *, bool /*onlyifmissing*/, bool /*snapshot*/);
#endif
