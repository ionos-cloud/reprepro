#ifndef REPREPRO_DESCRIPTIONS_H
#define REPREPRO_DESCRIPTIONS_H

struct description;



retvalue description_preparepackage(struct target*, const char */*package*/, const char */*control*/, /*@out@*/struct description**);

/* Do what is needed description/translation wise for a new package added.
 * control is the control chunk of the new package to be normalized
 * (depending on the target, towards containing full Description or checksumed),
 * oldcontrol is a old version of a package replaced or NULL
 * (for repairdescriptions oldcontrol==control).
 * newcontrol_p gets the new normalized control chunk.
 * description is the data returned by a description_prepareaddpackage or NULL
 * if that was not yet called
 */

retvalue description_addpackage(struct target*, const char */*package*/, const char */*control*/,/*@null@*/const char */*oldcontrol*/, /*@null@*/struct description*, /*@out@*/char **/*newcontrol_p*/);
#endif
