#ifndef __MIRRORER_REFERENCE_H
#define __MIRRORER_REFERENCE_H

DB *references_initialize(const char *dbpath);
int references_done(DB *db);
int references_removedependency(DB* refdb,const char *neededby);
int references_adddependency(DB* refdb,const char *needed,const char *neededby);

#endif
