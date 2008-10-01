#ifndef REPREPRO_ATOMS_H
#define REPREPRO_ATOMS_H

typedef int atom_t;
typedef atom_t architecture_t;
typedef atom_t component_t;
typedef atom_t packagetype_t;

#define atom_unknown ((atom_t)-1)
#define architecture_all ((atom_t)0)

extern const char **atoms_architectures, **atoms_components, **atoms_packagetype;

retvalue atoms_init(void);

retvalue architecture_intern(const char *, /*@out@*/architecture_t *);
architecture_t architecture_find(const char *);
retvalue component_intern(const char *, /*@out@*/component_t *);
component_t component_find(const char *);
packagetype_t packagetype_find(const char *);

#endif
