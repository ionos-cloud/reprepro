#ifndef REPREPRO_FILECNTL_H
#define REPREPRO_FILECNTL_H

#ifndef HAVE_CLOSEFROM
void closefrom(int);
#endif
void markcloseonexec(int);
void deletefile(const char *);

#endif
