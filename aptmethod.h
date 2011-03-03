#ifndef REPREPRO_APTMETHOD_H
#define REPREPRO_APTMETHOD_H

#ifndef REPREPRO_DATABASE_H
#include "database.h"
#endif
#ifndef REPREPRO_CHECKSUMS_H
#include "checksums.h"
#endif

struct aptmethodrun;
struct aptmethod;

struct tobedone {
	/*@null@*/
	struct tobedone *next;
	/* must be saved to know where is should be moved to: */
	/*@notnull@*/
	char *uri;
	/*@notnull@*/
	char *filename;
	/* if non-NULL, add to the database after found (needs md5sum != NULL) */
	union {
		/* if !indexfile */
		char *filekey;
		/* if indexfile, the place the final uncompressed checksum
		 * is to be saved to: */
		/*@null@*/ struct checksums **checksums_p;
	};
	union {
		/* if !indexfile */
		/*@null@*/ struct checksums *checksums;
		/* if !indexfile */
		/*@null@*/ const struct checksums *compressedchecksums;
	};
	bool indexfile;
	/* must be c_none if not a index file */
	enum compression compression;
};

retvalue aptmethod_initialize_run(/*@out@*/struct aptmethodrun **run);
retvalue aptmethod_newmethod(struct aptmethodrun *, const char *uri, const char *fallbackuri, const struct strlist *config, /*@out@*/struct aptmethod **);

retvalue aptmethod_queuefile(struct aptmethod *, const char *origfile, const char *destfile, const struct checksums *, const char *filekey, /*@out@*/struct tobedone **);
retvalue aptmethod_queueindexfile(struct aptmethod *method, const char *suite, const char *origfile, const char *destfile, /*@null@*/struct checksums **, enum compression, /*@null@*/const struct checksums *);

retvalue aptmethod_download(struct aptmethodrun *, struct database *);
retvalue aptmethod_shutdown(/*@only@*/struct aptmethodrun *);

#endif
