#ifndef __MIRRORER_REFERENCE_H
#define __MIRRORER_REFERENCE_H

DB *initialize_references(const char *dbpath);
int reference_removedepedency(DB* refdb,const char *neededby);
int reference_adddepedency(DB* refdb,const char *needed,const char *neededby);

#endif
