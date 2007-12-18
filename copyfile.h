#ifndef REPREPRO_COPYFILE_H
#define REPREPRO_COPYFILE_H

retvalue copy(const char *fullfilename,const char *origfile,/*@null@*/const char *md5expected,/*@null@*//*@out@*/char **calculatedmd5sum);
retvalue copyfile_hardlink(const char *mirrordir, const char *filekey, const char *tempfile, const char *md5sum);

#endif
