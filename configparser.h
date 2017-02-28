#ifndef REPREPRO_CONFIGPARSER_H
#define REPREPRO_CONFIGPARSER_H

#ifndef REPREPRO_STRLIST_H
#include "strlist.h"
#endif
#ifndef REPREPRO_CHECKS_H
#include "checks.h"
#endif
#ifndef REPREPRO_ATOMS_H
#include "atoms.h"
#endif

struct configiterator;

typedef retvalue configsetfunction(void *, const char *, void *, struct configiterator *);
typedef retvalue configinitfunction(void *, void *, void **);
typedef retvalue configfinishfunction(void *, void *, void **, bool, struct configiterator *);

retvalue linkedlistfinish(void *, void *, void **, bool, struct configiterator *);

struct configfield {
	const char *name;
	size_t namelen;
	/* privdata, allocated struct, iterator */
	configsetfunction *setfunc;
	bool required;
};

struct constant {
	const char *name;
	int value;
};

#define CFr(name, sname, field) {name, sizeof(name)-1, configparser_ ## sname ## _set_ ## field, true}
#define CF(name, sname, field) {name, sizeof(name)-1, configparser_ ## sname ## _set_ ## field, false}

/*@observer@*/const char *config_filename(const struct configiterator *) __attribute__((pure));
unsigned int config_line(const struct configiterator *) __attribute__((pure));
unsigned int config_column(const struct configiterator *) __attribute__((pure));
unsigned int config_firstline(const struct configiterator *) __attribute__((pure));
unsigned int config_markerline(const struct configiterator *) __attribute__((pure));
unsigned int config_markercolumn(const struct configiterator *) __attribute__((pure));
retvalue config_getflags(struct configiterator *, const char *, const struct constant *, bool *, bool, const char *);
int config_nextnonspaceinline(struct configiterator *iter);
retvalue config_getlines(struct configiterator *, struct strlist *);
retvalue config_getwords(struct configiterator *, struct strlist *);
retvalue config_getall(struct configiterator *iter, /*@out@*/char **result_p);
retvalue config_getword(struct configiterator *, /*@out@*/char **);
retvalue config_getwordinline(struct configiterator *, /*@out@*/char **);
retvalue config_geturl(struct configiterator *, const char *, /*@out@*/char **);
retvalue config_getonlyword(struct configiterator *, const char *, checkfunc, /*@out@*/char **);
retvalue config_getuniqwords(struct configiterator *, const char *, checkfunc, struct strlist *);
retvalue config_getinternatomlist(struct configiterator *, const char *, enum atom_type, checkfunc, struct atomlist *);
retvalue config_getatom(struct configiterator *, const char *, enum atom_type, atom_t *);
retvalue config_getatomlist(struct configiterator *, const char *, enum atom_type, struct atomlist *);
retvalue config_getatomsublist(struct configiterator *, const char *, enum atom_type, struct atomlist *, const struct atomlist *, const char *);
retvalue config_getsplitatoms(struct configiterator *, const char *, enum atom_type, struct atomlist *, struct atomlist *);
retvalue config_getsplitwords(struct configiterator *, const char *, struct strlist *, struct strlist *);
retvalue config_gettruth(struct configiterator *, const char *, bool *);
retvalue config_getnumber(struct configiterator *, const char *, long long *, long long /*minvalue*/, long long /*maxvalue*/);
retvalue config_getconstant(struct configiterator *, const struct constant *, int *);
#define config_getenum(iter, type, constants, result) ({int _val;retvalue _r = config_getconstant(iter, type ## _ ## constants, &_val);*(result) = (enum type)_val;_r;})
retvalue config_completeword(struct configiterator *, char, /*@out@*/char **);
retvalue config_gettimespan(struct configiterator *, const char *, /*@out@*/unsigned long *);
retvalue config_getscript(struct configiterator *, const char *, /*@out@*/char **);
retvalue config_getsignwith(struct configiterator *, const char *, struct strlist *);
void config_overline(struct configiterator *);
bool config_nextline(struct configiterator *);
retvalue configfile_parse(const char * /*filename*/, bool /*ignoreunknown*/, configinitfunction, configfinishfunction, const char *chunkname, const struct configfield *, size_t, void *);

#define CFlinkedlistinit(sname) \
static retvalue configparser_ ## sname ## _init(void *rootptr, void *lastitem, void **newptr) { \
	struct sname *n, **root_p = rootptr, *last = lastitem; \
	n = calloc(1, sizeof(struct sname)); \
	if (n == NULL) \
		return RET_ERROR_OOM; \
	if (last == NULL) \
		*root_p = n; \
	else \
		last->next = n; \
	*newptr = n; \
	return RET_OK; \
}
#define CFtimespanSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_gettimespan(iter, name, &item->field); \
}
#define CFcheckvalueSETPROC(sname, field, checker) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getonlyword(iter, name, checker, &item->field); \
}
#define CFvalueSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getonlyword(iter, name, NULL, &item->field); \
}
#define CFurlSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_geturl(iter, name, &item->field); \
}
#define CFscriptSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getscript(iter, name, &item->field); \
}
#define CFlinelistSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	item->field ## _set = true; \
	return config_getlines(iter, &item->field); \
}
#define CFstrlistSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getwords(iter, &item->field); \
}
#define CFsignwithSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getsignwith(iter, name, &item->field); \
}
#define CFcheckuniqstrlistSETPROC(sname, field, checker) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	retvalue r; \
	r = config_getuniqwords(iter, name, checker, &item->field); \
	if (r == RET_NOTHING) { \
		fprintf(stderr, \
"Error parsing %s, line %d, column %d:\n" \
" An empty %s-field is not allowed.\n", config_filename(iter), \
					config_line(iter), \
					config_column(iter), \
					name); \
		r = RET_ERROR; \
	} \
	return r; \
}
#define CFinternatomsSETPROC(sname, field, checker, type) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	retvalue r; \
	r = config_getinternatomlist(iter, name, type, checker, &item->field); \
	if (r == RET_NOTHING) { \
		fprintf(stderr, \
"Error parsing %s, line %d, column %d:\n" \
" An empty %s-field is not allowed.\n", config_filename(iter), \
					config_line(iter), \
					config_column(iter), \
					name); \
		r = RET_ERROR; \
	} \
	return r; \
}
#define CFatomlistSETPROC(sname, field, type) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	retvalue r; \
	item->field ## _set = true; \
	r = config_getatomlist(iter, name, type, &item->field); \
	if (r == RET_NOTHING) { \
		fprintf(stderr, \
"Error parsing %s, line %d, column %d:\n" \
" An empty %s-field is not allowed.\n", config_filename(iter), \
					config_line(iter), \
					config_column(iter), \
					name); \
		r = RET_ERROR; \
	} \
	return r; \
}
#define CFatomSETPROC(sname, field, type) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getatom(iter, name, type, &item->field); \
}
#define CFatomsublistSETPROC(sname, field, type, superset, superset_header) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	retvalue r; \
	item->field ## _set = true; \
	if (item->superset.count == 0) { \
		fprintf(stderr, \
"Error parsing %s, line %d, column %d:\n" \
" A '%s'-field is only allowed after a '%s'-field.\n", config_filename(iter), \
					config_line(iter), \
					config_column(iter), \
					name, superset_header); \
		return RET_ERROR; \
	} \
	r = config_getatomsublist(iter, name, type, &item->field, \
			&item->superset, superset_header); \
	if (r == RET_NOTHING) { \
		fprintf(stderr, \
"Error parsing %s, line %d, column %d:\n" \
" An empty %s-field is not allowed.\n", config_filename(iter), \
					config_line(iter), \
					config_column(iter), \
					name); \
		r = RET_ERROR; \
	} \
	return r; \
}
#define CFuniqstrlistSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getuniqwords(iter, name, NULL, &item->field); \
}
#define CFuniqstrlistSETPROCset(sname, name) \
static retvalue configparser_ ## sname ## _set_ ## name (UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	item->name ## _set = true; \
	return config_getuniqwords(iter, name, NULL, &item->name); \
}
#define CFtruthSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	item->field ## _set = true; \
	return config_gettruth(iter, name, &item->field); \
}
#define CFtruthSETPROC2(sname, name, field) \
static retvalue configparser_ ## sname ## _set_ ## name(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_gettruth(iter, name, &item->field); \
}
#define CFnumberSETPROC(sname, minval, maxval, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getnumber(iter, name, &item->field, minval, maxval); \
}
#define CFallSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return config_getall(iter, &item->field); \
}
#define CFfilterlistSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return filterlist_load(&item->field, iter); \
}
#define CFexportmodeSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	return exportmode_set(&item->field, iter); \
}
#define CFUSETPROC(sname, field) static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *thisdata_ ## sname, struct configiterator *iter)
#define CFuSETPROC(sname, field) static retvalue configparser_ ## sname ## _set_ ## field(void *privdata_ ## sname, UNUSED(const char *name), void *thisdata_ ## sname, struct configiterator *iter)
#define CFSETPROC(sname, field) static retvalue configparser_ ## sname ## _set_ ## field(void *privdata_ ## sname, const char *headername, void *thisdata_ ## sname, struct configiterator *iter)
#define CFSETPROCVARS(sname, item, mydata) struct sname *item = thisdata_ ## sname; struct read_ ## sname ## _data *mydata = privdata_ ## sname
#define CFSETPROCVAR(sname, item) struct sname *item = thisdata_ ## sname

#define CFstartparse(sname) static retvalue startparse ## sname(UNUSED(void *dummyprivdata), UNUSED(void *lastdata), void **result_p_ ##sname)
#define CFstartparseVAR(sname, r) struct sname **r = (void*)result_p_ ## sname
#define CFfinishparse(sname) static retvalue finishparse ## sname(void *privdata_ ## sname, void *thisdata_ ## sname, void **lastdata_p_ ##sname, bool complete, struct configiterator *iter)
#define CFfinishparseVARS(sname, this, last, mydata) struct sname *this = thisdata_ ## sname, **last = (void*)lastdata_p_ ## sname; struct read_ ## sname ## _data *mydata = privdata_ ## sname
#define CFUfinishparseVARS(sname, this, last, mydata) struct sname *this = thisdata_ ## sname
#define CFhashesSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), const char *name, void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	retvalue r; \
	item->field ## _set = true; \
	r = config_getflags(iter, name, hashnames, item->field, false, \
			"(allowed values: md5, sha1 and sha256)"); \
	if (!RET_IS_OK(r)) \
		return r; \
	return RET_OK; \
}

// TODO: better error reporting:
#define CFtermSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	char *formula; \
	retvalue r; \
	r = config_getall(iter, &formula); \
	if (! RET_IS_OK(r)) \
		return r; \
	r = term_compilefortargetdecision(&item->field, formula); \
	free(formula); \
	return r; \
}
#define CFtermSSETPROC(sname, field) \
static retvalue configparser_ ## sname ## _set_ ## field(UNUSED(void *dummy), UNUSED(const char *name), void *data, struct configiterator *iter) { \
	struct sname *item = data; \
	char *formula; \
	retvalue r; \
	r = config_getall(iter, &formula); \
	if (! RET_IS_OK(r)) \
		return r; \
	r = term_compilefortargetdecision(&item->field, formula); \
	free(formula); \
	item->field ## _set = true; \
	return r; \
}

// TODO: decide which should get better checking, which might allow escaping spaces:
#define CFdirSETPROC CFvalueSETPROC
#define CFfileSETPROC CFvalueSETPROC
#define config_getfileinline config_getwordinline

char *configfile_expandname(const char *, /*@only@*//*@null@*/char *);
#endif /* REPREPRO_CONFIGPARSER_H */
