#ifndef REPREPRO_COPYFILE_H
#define REPREPRO_COPYFILE_H

retvalue copyfile_copy(const char *mirrordir,const char *filekey,const char *origfile,/*@null@*/const char *md5expected,/*@out@*/char **md5sum);
retvalue copyfile_move(const char *mirrordir,const char *filekey,const char *origfile,/*@null@*/const char *md5expected,/*@out@*/char **md5sum);
void copyfile_delete(const char *fullfilename);
retvalue regularfileexists(const char *fullfilename);
bool_t isregularfile(const char *fullfilename);

#endif
