#include "freebsd_user.h"
#include <sys/kern_syscalls.h>
#include <sys/stat.h>
#include <string.h>

/* FreeBSD Stat Translation */
static void translate_stat_to_freebsd(struct stat *native, struct freebsd_stat *fbsd) {
    memset(fbsd, 0, sizeof(struct freebsd_stat));
    fbsd->st_dev = native->st_dev;
    fbsd->st_ino = native->st_ino;
    fbsd->st_mode = (uint16_t)native->st_mode;
    fbsd->st_nlink = (uint16_t)native->st_nlink;
    fbsd->st_uid = native->st_uid;
    fbsd->st_gid = native->st_gid;
    fbsd->st_rdev = native->st_rdev;
    fbsd->st_atim.tv_sec = (int32_t)native->st_atime;
    fbsd->st_atim.tv_nsec = (int32_t)native->st_atime_nsec;
    fbsd->st_mtim.tv_sec = (int32_t)native->st_mtime;
    fbsd->st_mtim.tv_nsec = (int32_t)native->st_mtime_nsec;
    fbsd->st_ctim.tv_sec = (int32_t)native->st_ctime;
    fbsd->st_ctim.tv_nsec = (int32_t)native->st_ctime_nsec;
    fbsd->st_size = native->st_size;
    fbsd->st_blocks = native->st_blocks;
    fbsd->st_blksize = native->st_blksize;
}

int sys_freebsd_stat(const char *path, struct freebsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct freebsd_stat kfbsd;
        translate_stat_to_freebsd(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd_stat)) != 0) return -14;
    }
    return ret;
}

int sys_freebsd_lstat(const char *path, struct freebsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct freebsd_stat kfbsd;
        translate_stat_to_freebsd(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd_stat)) != 0) return -14;
    }
    return ret;
}

int sys_freebsd_fstat(int fd, struct freebsd_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct freebsd_stat kfbsd;
        translate_stat_to_freebsd(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd_stat)) != 0) return -14;
    }
    return ret;
}

/* FreeBSD 11 Stat Translation */
static void translate_stat_to_freebsd11(struct stat *native, struct freebsd11_stat *fbsd) {
    memset(fbsd, 0, sizeof(struct freebsd11_stat));
    fbsd->st_dev = native->st_dev;
    fbsd->st_ino = (uint32_t)native->st_ino;
    fbsd->st_mode = (uint16_t)native->st_mode;
    fbsd->st_nlink = (uint16_t)native->st_nlink;
    fbsd->st_uid = native->st_uid;
    fbsd->st_gid = native->st_gid;
    fbsd->st_rdev = native->st_rdev;
    fbsd->st_atim.tv_sec = (int32_t)native->st_atime;
    fbsd->st_atim.tv_nsec = (int32_t)native->st_atime_nsec;
    fbsd->st_mtim.tv_sec = (int32_t)native->st_mtime;
    fbsd->st_mtim.tv_nsec = (int32_t)native->st_mtime_nsec;
    fbsd->st_ctim.tv_sec = (int32_t)native->st_ctime;
    fbsd->st_ctim.tv_nsec = (int32_t)native->st_ctime_nsec;
    fbsd->st_size = native->st_size;
    fbsd->st_blocks = native->st_blocks;
    fbsd->st_blksize = native->st_blksize;
}

int sys_freebsd11_stat(const char *path, struct freebsd11_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct freebsd11_stat kfbsd;
        translate_stat_to_freebsd11(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd11_stat)) != 0) return -14;
    }
    return ret;
}

int sys_freebsd11_lstat(const char *path, struct freebsd11_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct freebsd11_stat kfbsd;
        translate_stat_to_freebsd11(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd11_stat)) != 0) return -14;
    }
    return ret;
}

int sys_freebsd11_fstat(int fd, struct freebsd11_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct freebsd11_stat kfbsd;
        translate_stat_to_freebsd11(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd11_stat)) != 0) return -14;
    }
    return ret;
}
