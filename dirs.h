#ifndef __MIRRORER_DIRS_H
#define __MIRRORER_DIRS_H

/* everything here returns -1 on error, 0 on success. */

/* create recursively all parent directories before the last '/' */
int make_parent_dirs(const char *filename);
/* create dirname and any '/'-seperated part of it */
int make_dir_recursive(const char *filename);

#endif
