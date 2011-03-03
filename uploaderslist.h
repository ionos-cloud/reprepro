#ifndef REPREPRO_UPLOADERSLIST_H
#define REPREPRO_UPLOADERSLIST_H

struct uploadpermissions {
	/* later more fine grained permissions */
	bool allowall;
};

struct uploaders;

retvalue uploaders_get(/*@out@*/struct uploaders **list, const char *confdir, const char *filename);
void uploaders_unlock(/*@only@*//*@null@*/struct uploaders *);

retvalue uploaders_unsignedpermissions(struct uploaders *,
		/*@out@*/const struct uploadpermissions **);
retvalue uploaders_permissions(struct uploaders *,const char *fingerprint,
		/*@out@*/const struct uploadpermissions **);

#endif
