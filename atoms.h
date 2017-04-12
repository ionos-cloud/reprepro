#ifndef REPREPRO_ATOMS_H
#define REPREPRO_ATOMS_H

typedef int atom_t;
typedef atom_t architecture_t;
typedef atom_t component_t;
typedef atom_t packagetype_t;
typedef atom_t command_t;

enum atom_type { at_architecture, at_component, at_packagetype, at_command };

#define atom_unknown ((atom_t)0)

#define architecture_source ((architecture_t)1)
#define architecture_all ((architecture_t)2)

#define component_strange ((component_t)1)

#define pt_dsc ((packagetype_t)1)
#define pt_deb ((packagetype_t)2)
#define pt_udeb ((packagetype_t)3)
#define pt_ddeb ((packagetype_t)4)

#define atom_defined(a) ((a) > (atom_t)0)

extern const char **atomtypes, **atoms_architectures, **atoms_components, **atoms_packagetypes, **atoms_commands;

retvalue atoms_init(int command_count);

retvalue architecture_intern(const char *, /*@out@*/architecture_t *);
architecture_t architecture_find(const char *);
architecture_t architecture_find_l(const char *, size_t);
retvalue component_intern(const char *, /*@out@*/component_t *);
component_t component_find(const char *);
component_t component_find_l(const char *, size_t);
component_t components_count(void);
packagetype_t packagetype_find(const char *);
packagetype_t packagetype_find_l(const char *, size_t);

atom_t atom_find(enum atom_type, const char *);
retvalue atom_intern(enum atom_type, const char *, /*@out@*/atom_t *);

#define limitation_missed(a, b) ((atom_defined(a) && (a) != (b)))
#define limitations_missed(a, b) ((a) != NULL && !atomlist_in(a, b))


struct atomlist {
	atom_t *atoms;
	int count, size;
};

void atomlist_init(/*@out@*/struct atomlist *);
void atomlist_done(/*@special@*/struct atomlist *atomlist) /*@releases atomlist->values @*/;

/* add a atom uniquely (not sorted, component guessing might not like it),
 * RET_NOTHING when already there */
retvalue atomlist_add_uniq(struct atomlist *, atom_t);
/* always add to the end */
retvalue atomlist_add(struct atomlist *, atom_t);

/* replace the contents of dest with those from orig, which get emptied */
void atomlist_move(/*@out@*/struct atomlist *, /*@special@*/struct atomlist *orig) /*@releases orig->values @*/;

bool atomlist_hasexcept(const struct atomlist *, atom_t);
bool atomlist_in(const struct atomlist *, atom_t);
int atomlist_ofs(const struct atomlist *, atom_t);

/* if missing != NULL And subset no subset of atomlist, set *missing to the first missing one */
bool atomlist_subset(const struct atomlist *, const struct atomlist * /*subset*/, /*@null@*/atom_t * /*missing*/ );

/* print a space separated list of elements */
retvalue atomlist_fprint(FILE *, enum atom_type, const struct atomlist *);

retvalue atomlist_filllist(enum atom_type, /*@out@*/struct atomlist *, char * /*string*/, /*@out@*/const char ** /*missing*/);
#endif
