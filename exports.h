#ifndef REPREPRO_EXPORTS_H
#define REPREPRO_EXPORTS_H

typedef enum {ic_uncompressed=0, ic_gzip } indexcompression;
#define ic_max ic_gzip

struct exportmode {
	/* "Packages", "Sources" or something like that */ 
	char *filename;
	/* create uncompressed, create .gz, <future things...> */
	bool_t compressions[ic_max+1];
	/* Generate a Release file next to the Indexfile , if non-null*/
	/*@null@*/
	char *release;
	/* programm to start after all are generated */
	char *hook;
};

retvalue exportmode_init(/*@out@*/struct exportmode *mode,bool_t uncompressed,/*@null@*/const char *release,const char *indexfile,/*@null@*//*@only@*/char *options);
void exportmode_done(struct exportmode *mode);

retvalue export_target(const char *confdir,const char *dirofdist,const char *relativedir,packagesdb packages,const struct exportmode *exportmode,struct strlist *releasedfiles, bool_t onlymissing, int force);
retvalue export_checksums(const char *dirofdist,FILE *f,struct strlist *releasedfiles, int force);
retvalue export_finalize(const char *dirofdist,struct strlist *releasedfiles, int force, bool_t issigned);

#endif
