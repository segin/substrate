#include "linux_user.h"
#include <sys/kern_syscalls.h>
#include <string.h>
#include <sys/errno.h>

/* Linux stat translation: native -> linux_stat */
int linux_sys_stat(const char *path, struct linux_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    
    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret < 0) return ret;
    
    struct linux_stat kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -14;
    return 0;
}

int linux_sys_lstat(const char *path, struct linux_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret < 0) return ret;
    
    struct linux_stat kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -14;
    return 0;
}

int linux_sys_fstat(int fd, struct linux_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret < 0) return ret;
    
    struct linux_stat kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -14;
    return 0;
}

/* Linux stat64 translation: native -> linux_stat64 */
int linux_sys_stat64(const char *path, struct linux_stat64 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret < 0) return ret;
    
    struct linux_stat64 kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.__st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = native.st_size;
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = native.st_blocks;
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    kbuf.st_ino = native.st_ino;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -14;
    return 0;
}

int linux_sys_lstat64(const char *path, struct linux_stat64 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret < 0) return ret;
    
    struct linux_stat64 kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.__st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = native.st_size;
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = native.st_blocks;
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    kbuf.st_ino = native.st_ino;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -14;
    return 0;
}

int linux_sys_fstat64(int fd, struct linux_stat64 *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret < 0) return ret;
    
    struct linux_stat64 kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.__st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = native.st_size;
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = native.st_blocks;
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    kbuf.st_ino = native.st_ino;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -14;
    return 0;
}
