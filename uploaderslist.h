#ifndef REPREPRO_UPLOADERSLIST_H
#define REPREPRO_UPLOADERSLIST_H


struct upload_conditions;
struct uploaders;

enum upload_condition_type { uc_REJECTED = 0, uc_ALWAYS,
/*	uc_COMPONENT, */ uc_ARCHITECTURES,
	uc_CODENAME,
	uc_SOURCENAME, uc_SECTIONS, uc_BINARIES, uc_BYHAND };
#define uc_ACCEPTED uc_ALWAYS

retvalue uploaders_get(/*@out@*/struct uploaders **list, const char *filename);
void uploaders_unlock(/*@only@*//*@null@*/struct uploaders *);

struct signatures;
retvalue uploaders_permissions(struct uploaders *, const struct signatures *, /*@out@*/struct upload_conditions **);

/* uc_FAILED means rejected, uc_ACCEPTED means can go in */
enum upload_condition_type uploaders_nextcondition(struct upload_conditions *);
/* true means, give more if more to check, false means enough */
bool uploaders_verifystring(struct upload_conditions *, const char *);
bool uploaders_verifyatom(struct upload_conditions *, atom_t);

#endif
