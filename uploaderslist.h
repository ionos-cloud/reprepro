#ifndef REPREPRO_UPLOADERSLIST_H
#define REPREPRO_UPLOADERSLIST_H

struct uploadpermissions {
	/* later more fine grained permissions */
	bool_t allowall;
};

struct uploaders;

retvalue uploaders_load(/*@out@*/struct uploaders **list, const char *confdir, const char *filename);
void uploaders_free(/*@only@*//*@null@*/struct uploaders *);

retvalue uploaders_unsignedpermissions(struct uploaders *,
		/*@out@*/const struct uploadpermissions **);
retvalue uploaders_permissions(struct uploaders *,const char *fingerprint,
		/*@out@*/const struct uploadpermissions **);

#endif
