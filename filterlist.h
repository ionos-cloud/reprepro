#ifndef REPREPRO_FILTERLIST_H
#define REPREPRO_FILTERLIST_H

enum filterlisttype {
	flt_purge,
	flt_deinstall,
	flt_install,
	flt_hold,
	flt_error
};

struct filterlistfile;

struct filterlist {
	size_t count;
	struct filterlistfile **files;

	/* to be used when not found */
	enum filterlisttype defaulttype;
};


retvalue filterlist_load(/*@out@*/struct filterlist *list, const char *confdir, const char *filename);
void filterlist_empty(/*@out@*/struct filterlist *list, enum filterlisttype defaulttype);

void filterlist_release(struct filterlist *list);

enum filterlisttype filterlist_find(const char *name,struct filterlist *root);

#endif
