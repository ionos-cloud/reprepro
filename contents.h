#ifndef REPREPRO_CONTENTS_H
#define REPREPRO_CONTENTS_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_RELEASE_H
#include "release.h"
#endif

struct contentsoptions {
	struct {
		bool enabled:1;
		bool udebs:1;
		bool nodebs:1;
	} flags;
	compressionset compressions;
};

struct distribution;
struct configiterator;

retvalue contentsoptions_parse(struct distribution *distribution, struct configiterator *iter);
retvalue contents_generate(struct database *, struct distribution *, struct release *, bool onlyneeded);

#endif
