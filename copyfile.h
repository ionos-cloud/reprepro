#ifndef __MIRRORER_COPYFILE_H
#define __MIRRORER_COPYFILE_H

retvalue copyfile_copy(const char *mirrordir,const char *filekey,const char *origfile,const char *md5expected,char **md5sum);
retvalue copyfile_move(const char *mirrordir,const char *filekey,const char *origfile,const char *md5expected,char **md5sum);
void copyfile_delete(const char *fullfilename);

#endif
