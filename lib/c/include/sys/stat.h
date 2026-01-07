#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

struct stat {
    dev_t          st_dev;
    unsigned long  st_ino;
    mode_t         st_mode;
    unsigned short st_nlink;
    uid_t          st_uid;
    gid_t          st_gid;
    dev_t          st_rdev;
    off_t          st_size;
    unsigned long  st_blksize;
    blkcnt_t       st_blocks;
    time_t         st_atime;
    unsigned long  st_atime_nsec;
    time_t         st_mtime;
    unsigned long  st_mtime_nsec;
    time_t         st_ctime;
    unsigned long  st_ctime_nsec;
};

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFREG 0100000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000

#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

int mkdir(const char *pathname, int mode);
int stat(const char *pathname, struct stat *statbuf);
int mknod(const char *pathname, mode_t mode, dev_t dev);

#endif
