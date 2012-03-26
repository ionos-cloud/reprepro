#ifndef REPREPRO_FILTERLIST_H
#define REPREPRO_FILTERLIST_H

enum filterlisttype {
	/* must be 0, so it is the default, when there is no list */
	flt_install = 0,
	flt_unchanged, /* special value used by the cmdline lists */
	flt_auto_hold, /* special value used by the cmdline lists */
	flt_purge,
	flt_warning,
	flt_deinstall,
	flt_hold,
	flt_supersede,
	flt_upgradeonly,
	flt_error
};

struct filterlistfile;

struct filterlist {
	size_t count;
	struct filterlistfile **files;

	/* to be used when not found */
	enum filterlisttype defaulttype;
	/* true if this is loaded from config */
	bool set;
};

struct configiterator;
retvalue filterlist_load(/*@out@*/struct filterlist *, struct configiterator *);

void filterlist_release(struct filterlist *list);

enum filterlisttype filterlist_find(const char *name, const char *version, const struct filterlist *);

extern struct filterlist cmdline_bin_filter, cmdline_src_filter;
retvalue filterlist_cmdline_add_pkg(bool, const char *);
retvalue filterlist_cmdline_add_file(bool, const char *);

#endif
