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
	/* programm to start after all are generated */
	char *hook;
};

retvalue exportmode_init(/*@out@*/struct exportmode *mode, bool uncompressed, /*@null@*/const char *release, const char *indexfile);
struct configiterator;
retvalue exportmode_set(struct exportmode *mode, struct configiterator *iter);
void exportmode_done(struct exportmode *mode);

retvalue export_target(const char *relativedir, struct target *, struct database *, const struct exportmode *, struct release *, bool onlyifmissing, bool snapshot);
#endif
