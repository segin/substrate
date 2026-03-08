#ifndef _SUNOS_USER_H
#define _SUNOS_USER_H

#include <stdint.h>

struct sunos_stat {
    uint16_t st_dev;
    uint16_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint16_t st_rdev;
    int32_t  st_size;
    int32_t  st_atime;
    int32_t  st_spare1;
    int32_t  st_mtime;
    int32_t  st_spare2;
    int32_t  st_ctime;
    int32_t  st_spare3;
    int32_t  st_blksize;
    int32_t  st_blocks;
    int32_t  st_spare4[2];
};

int sunos_sys_stat(const char *path, struct sunos_stat *buf);
int sunos_sys_lstat(const char *path, struct sunos_stat *buf);
int sunos_sys_fstat(int fd, struct sunos_stat *buf);

#endif /* _SUNOS_USER_H */
