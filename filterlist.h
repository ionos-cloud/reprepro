#ifndef REPREPRO_FILTERLIST_H
#define REPREPRO_FILTERLIST_H

struct filterlist {
	struct filterlist *next;
	char *packagename;
	enum filterlisttype {
		flt_purge,
		flt_deinstall,
		flt_install,
		flt_hold
	} what;
};


retvalue filterlist_load(struct filterlist **list, const char *confdir, const char *filename);
void filterlist_free(struct filterlist *list);

bool_t filterlist_find(const char *name,struct filterlist *root,const struct filterlist **last_p);

#endif
