#ifndef REPREPRO_TERMS_H
#define REPREPRO_TERMS_H

enum term_comparison { tc_none=0, tc_equal, tc_strictless, tc_strictmore,
				  tc_lessorequal, tc_moreorequal,
				  tc_notequal, tc_globmatch, tc_notglobmatch};

struct term_special;

typedef struct term_atom {
	/* global list to allow freeing them all */
	struct term_atom *next;
	/* the next atom to look at if this is true, resp. false,
	 * nextiftrue  == NULL means total result is true,
	 * nextiffalse == NULL means total result is false. */
	/*@dependent@*/struct term_atom *nextiftrue, *nextiffalse;
	bool negated, isspecial;
	/* architecture requirements */
	bool architectures_negated;
	struct strlist architectures;
	/* version/value requirement */
	enum term_comparison comparison;
	union {
		struct {
			/* package-name or key */
			char *key;
			/* version/value requirement */
			char *comparewith;
		} generic;
		struct {
			const struct term_special *type;
			struct compare_with {
				void *pointer;
				long number;
			} comparewith;
		} special;
	};
} term;

struct term_special {
	const char *name;
	retvalue (*parse)(enum term_comparison, const char *, size_t len, struct compare_with *);
	bool (*compare)(enum term_comparison, const struct compare_with *, void*, void*);
	void (*done)(enum term_comparison, struct compare_with *);
};

/* | is allowed in terms */
#define T_OR		0x01
/* () are allowed to build sub-expressions */
#define T_BRACKETS	0x02
/* expressions may be negated */
#define T_NEGATION	0x04
/* (<rel> <version>) is allowed */
#define T_VERSION 	0x10
/* [archlist] is allowed */
#define T_ARCHITECTURES 0x20
/* (!= value) is allowed */
#define T_NOTEQUAL	0x40
/* (% <globpattern>) and (!% globpattern) are allowed */
#define T_GLOBMATCH	0x80

retvalue term_compile(/*@out@*/term **, const char * /*formula*/, int /*options*/, /*@null@*/const struct term_special *specials);
void term_free(/*@null@*//*@only@*/term *);

#endif
