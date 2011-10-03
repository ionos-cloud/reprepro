#ifndef REPREPRO_FILECNTL_H
#define REPREPRO_FILECNTL_H

#ifndef HAVE_CLOSEFROM
void closefrom(int);
#endif
void markcloseonexec(int);
int deletefile(const char *);
bool isanyfile(const char *);
bool isregularfile(const char *);
bool isdirectory(const char *fullfilename);

#endif
