#ifndef REPREPRO_COPYFILE_H
#define REPREPRO_COPYFILE_H

retvalue copyfile_copy(const char *mirrordir,const char *filekey,const char *origfile,const char *md5expected,char **md5sum);
retvalue copyfile_move(const char *mirrordir,const char *filekey,const char *origfile,const char *md5expected,char **md5sum);
void copyfile_delete(const char *fullfilename);
retvalue regularfileexists(const char *fullfilename);

#endif
