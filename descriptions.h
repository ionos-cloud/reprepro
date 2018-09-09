#ifndef REPREPRO_DESCRIPTIONS_H
#define REPREPRO_DESCRIPTIONS_H

/* Do what is needed description/translation wise for a new package added.
 * control is the control chunk of the new package to be normalized
 * (depending on the target, towards containing full Description or checksumed),
 * newcontrol_p gets the new normalized control chunk.
 */

retvalue description_addpackage(struct target*, const char */*package*/, const char */*control*/, /*@out@*/char **/*newcontrol_p*/);
#endif
