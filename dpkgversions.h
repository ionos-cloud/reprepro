#ifndef __MIRRORER_DPKGVERSIONS
#define __MIRRORER_DPKGVERSIONS

/* code taken from dpkg to compare to version string, returns
 * -2 on parsing errors, 1 if newer, 0 if not */
int isVersionNewer(const char *first,const char *second);

#endif
