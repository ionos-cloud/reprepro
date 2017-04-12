#ifndef REPREPRO_DISTRIBUTION_H
#define REPREPRO_DISTRIBUTION_H

struct distribution;

#ifndef REPREPRO_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_TARGET_H
#include "target.h"
#endif
#ifndef REPREPRO_EXPORTS_H
#include "exports.h"
#endif
#ifndef REPREPRO_CONTENTS_H
#include "contents.h"
#endif
struct overridefile;
struct uploaders;

enum exportoptions {
	deo_noexport = 0,
	deo_keepunknown,
	deo_COUNT,
};

struct distribution {
	struct distribution *next;
	/* the primary name to access this distribution: */
	char *codename;
	/* for more helpfull error messages: */
	const char *filename; /* only valid while parsing! */
	unsigned int firstline, lastline;
	/* additional information for the Release-file to be
	 * generated, may be NULL. only suite is sometimes used
	 * (and only for sanity checks) */
	/*@null@*/char *suite, *version;
	/*@null@*/char *origin, *label, *description,
		*notautomatic, *butautomaticupgrades, *signed_by;
	/* What architectures and components are there */
	struct atomlist architectures, components;
	/* which update rules to use */
	struct strlist updates;
	/* which rules to use to pull packages from other distributions */
	struct strlist pulls;
	/* the key to sign with, may have no entries to mean unsigned: */
	struct strlist signwith;
	long long limit;
	/* the codename of the archive distribution (when the limit is exceeded) */
	/*@null@*/struct distribution *archive;
	/* the override file to use by default */
	/*@null@*/char *deb_override, *udeb_override, *dsc_override;
	/* fake component prefix (and codename antisuffix) for Release files: */
	/*@null@*/char *fakecomponentprefix;
	/* only loaded when you've done it yourself: */
	struct {
		/*@null@*/struct overridefile *dsc, *deb, *udeb;
	} overrides;
	/* the list of components containing a debian-installer dir,
	 * normally only "main" */
	struct atomlist udebcomponents;
	/* the list of components containing a debug directory */
	struct atomlist ddebcomponents;
	/* what kind of index files to generate */
	struct exportmode dsc, deb, udeb, ddeb;
	bool exportoptions[deo_COUNT];
	/* (NONE must be 0 so it is the default) */
	enum trackingtype { dt_NONE=0, dt_KEEP, dt_ALL, dt_MINIMAL } tracking;
	struct trackingoptions { bool includechanges;
		bool includebyhand;
		bool includebuildinfos;
		bool includelogs;
		bool needsources;
		bool keepsources;
		bool embargoalls;
		} trackingoptions;
	trackingdb trackingdb;
	/* what content files to generate */
	struct contentsoptions contents;
	struct atomlist contents_architectures,
		       contents_components,
		       contents_dcomponents,
		       contents_ucomponents;
	bool contents_architectures_set,
		       contents_components_set,
		       contents_dcomponents_set,
		       contents_ucomponents_set,
		       /* not used, just here to keep things simpler: */
		       ddebcomponents_set,
		       udebcomponents_set;
	/* A list of all targets contained in the distribution*/
	struct target *targets;
	/* a filename to look for who is allowed to upload packages */
	/*@null@*/char *uploaders;
	/* only loaded after _loaduploaders */
	/*@null@*/struct uploaders *uploaderslist;
	/* how and where to log */
	/*@null@*/struct logger *logger;
	/* scripts to feed byhand/raw-* files in */
	/*@null@*/struct byhandhook *byhandhooks;
	/* a list of names beside Codename and Suite to accept .changes
	 * files via include */
	struct strlist alsoaccept;
	/* if != 0, number of seconds to add for Vaild-Until */
	unsigned long validfor;
	/* RET_NOTHING: do not export with EXPORT_CHANGED, EXPORT_NEVER
	 * RET_OK: export unless EXPORT_NEVER
	 * RET_ERROR_*: only export with EXPORT_FORCE */
	retvalue status;
	/* false: not looked at, do not export at all */
	bool lookedat;
	/* false: not requested, do not handle at all */
	bool selected;
	/* forbid all writing operations and exports if true */
	bool readonly;
	/* tracking information might be obsolete */
	bool needretrack;
	/* omitted because of --onlysmalldeletes */
	bool omitted;
};

retvalue distribution_get(struct distribution * /*all*/, const char *, bool /*lookedat*/, /*@out@*/struct distribution **);

/* set lookedat, start logger, ... */
retvalue distribution_prepareforwriting(struct distribution *);

/*@dependent@*/struct target *distribution_getpart(const struct distribution *distribution, component_t, architecture_t, packagetype_t);

/* like distribtion_getpart, but returns NULL if there is no such target */
/*@null@*//*@dependent@*/struct target *distribution_gettarget(const struct distribution *distribution, component_t, architecture_t, packagetype_t);

retvalue distribution_fullexport(struct distribution *);


retvalue distribution_snapshot(struct distribution *, const char */*name*/);

/* read the configuration from all distributions */
retvalue distribution_readall(/*@out@*/struct distribution **distributions);

/* mark all dists from <conf> fitting in the filter given in <argc, argv> */
retvalue distribution_match(struct distribution * /*alldistributions*/, int /*argc*/, const char * /*argv*/ [], bool /*lookedat*/, bool /*readonly*/);

/* get a pointer to the apropiate part of the linked list */
struct distribution *distribution_find(struct distribution *, const char *);

retvalue distribution_freelist(/*@only@*/struct distribution *distributions);
enum exportwhen {EXPORT_NEVER, EXPORT_SILENT_NEVER, EXPORT_CHANGED, EXPORT_NORMAL, EXPORT_FORCE };
retvalue distribution_exportlist(enum exportwhen when, /*@only@*/struct distribution *);

retvalue distribution_loadalloverrides(struct distribution *);
void distribution_unloadoverrides(struct distribution *distribution);

retvalue distribution_loaduploaders(struct distribution *);
void distribution_unloaduploaders(struct distribution *distribution);
#endif
