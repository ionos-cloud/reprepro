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

retvalue exportmode_init(/*@out@*/struct exportmode *mode,bool_t uncompressed,/*@null@*/const char *release,const char *indexfile,/*@null@*//*@only@*/char *options);
void exportmode_done(struct exportmode *mode);

retvalue export_target(const char *confdir,const char *relativedir,packagesdb packages,const struct exportmode *exportmode,struct release *release, bool_t onlymissing);

#endif
