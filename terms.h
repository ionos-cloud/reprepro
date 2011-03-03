#ifndef REPREPRO_TERMS_H
#define REPREPRO_TERMS_H

enum term_comparison { tc_none=0, tc_equal, tc_strictless, tc_strictmore,
				  tc_lessorequal, tc_moreorequal,
				  tc_notequal};

typedef struct term_atom {
	/* global list to allow freeing them all */
	struct term_atom *next;
	/* the next atom to look at if this is true, resp. false,
	 * nextiftrue  == NULL means total result is true,
	 * nextiffalse == NULL means total result is false. */
	/*@dependent@*/struct term_atom *nextiftrue,*nextiffalse;
	bool negated;

	/* package-name or key */
	char *key;
	/* version/value requirement */
	enum term_comparison comparison;
	char *comparewith;
	/* architecture requirements */
	bool architectures_negated;
	struct strlist architectures;
} term;

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

retvalue term_compile(/*@out@*/term **term, const char *formula, int options);
void term_free(/*@null@*//*@only@*/term *term);

/* decide based on a chunk, (warning: string comparisons even for version!)*/
retvalue term_decidechunk(term *condition,const char *controlchunk);

#endif
