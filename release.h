#ifndef __MIRRORER_RELEASE_H
#define __MIRRORER_RELEASE_H

#ifndef __MIRRORER_ERROR_H
#include "error.h"
#warning "What's hapening here?"
#endif
#include "strlist.h"

/* get a strlist with the md5sums of a Release-file */
retvalue release_getchecksums(const char *releasefile,struct strlist *info);
/* check in fileinfo for <nametocheck> to have md5sum and size <expected> *
 * returns RET_OK if ok, == RET_NOTHING if not found, error otherwise     */
retvalue release_searchchecksum(const struct strlist *fileinfos, const char *nametocheck, const char *expected);

/* check in fileinfo for <nametocheck> to have md5sum and size of <filename> *
 * returns RET_OK if ok, error otherwise     */
retvalue release_check(const struct strlist *fileinfos, const char *nametocheck, const char *filename);
	
/* check for a <filetocheck> to be have same md5sum and size as <nametocheck> in <releasefile>,
 * returns 1 if ok, == 0 if <nametocheck> not specified, != 1 on error */
retvalue release_checkfile(const char *releasefile,const char *nametocheck,const char *filetocheck);

struct release {
	char *codename,*suite,*version;
	char *origin,*label,*description;
	struct strlist architectures,components;
};

void release_free(struct release *release);
retvalue release_parse(struct release **release,const char *chunk);

struct release_filter {int count; char **dists; };
retvalue release_parse_and_filter(struct release **release,const char *chunk,struct release_filter filter);

/* Generate a "Release"-file for binary directory */
retvalue release_genbinary(const struct release *release,const char *arch,const char *component,const char *distdir);

/* Generate a "Release"-file for source directory */
retvalue release_gensource(const struct release *release,const char *component,const char *distdir);

/* Generate a main "Release" file for a distribution */
retvalue release_gen(const struct release *release,const char *distdir);

typedef retvalue release_each_source_action(void *data, const char *component);
typedef retvalue release_each_binary_action(void *data, const char *component, const char *arch);

/* call <sourceaction> for each source part of <release> and <binaction> for each binary part of it. */
retvalue release_foreach_part(const struct release *release,release_each_source_action sourceaction,release_each_binary_action binaction,void *data);


typedef retvalue releaseaction(void *data,const char *chunk,const struct release *release);

retvalue release_foreach(const char *conf,int argc,char *argv[],releaseaction action,void *data,int force);

#endif
