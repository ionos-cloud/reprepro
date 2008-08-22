#ifndef REPREPRO_FILECNTL_H
#define REPREPRO_FILECNTL_H

#ifndef HAVE_CLOSEFROM
void closefrom(int);
#endif
void markcloseonexec(int);
int deletefile(const char *);
bool isregularfile(const char *);

#endif
