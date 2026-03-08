#include "netbsd_user.h"
#include <sys/kern_syscalls.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/errno.h>
#include <stddef.h>

/* Helper to translate native stat to NetBSD stat */
static void translate_stat_to_netbsd(const struct stat *native, struct netbsd_stat *nbsd) {
    memset(nbsd, 0, sizeof(*nbsd));
    nbsd->st_dev = native->st_dev;
    nbsd->st_ino = (uint32_t)native->st_ino;
    nbsd->st_mode = native->st_mode;
    nbsd->st_nlink = native->st_nlink;
    nbsd->st_uid = native->st_uid;
    nbsd->st_gid = native->st_gid;
    nbsd->st_rdev = native->st_rdev;
    nbsd->st_atime = (int32_t)native->st_atime;
    nbsd->st_atimensec = (int32_t)native->st_atime_nsec;
    nbsd->st_mtime = (int32_t)native->st_mtime;
    nbsd->st_mtimensec = (int32_t)native->st_mtime_nsec;
    nbsd->st_ctime = (int32_t)native->st_ctime;
    nbsd->st_ctimensec = (int32_t)native->st_ctime_nsec;
    nbsd->st_size = native->st_size;
    nbsd->st_blocks = native->st_blocks;
    nbsd->st_blksize = native->st_blksize;
    nbsd->st_flags = 0;
    nbsd->st_gen = 0;
}

/* Helper to translate native stat to NetBSD stat43 (older compat) */
static void translate_stat_to_netbsd43(const struct stat *native, struct netbsd_stat43 *nbsd43) {
    memset(nbsd43, 0, sizeof(*nbsd43));
    nbsd43->st_dev = (uint16_t)native->st_dev;
    nbsd43->st_ino = (uint32_t)native->st_ino;
    nbsd43->st_mode = native->st_mode;
    nbsd43->st_nlink = native->st_nlink;
    nbsd43->st_uid = native->st_uid;
    nbsd43->st_gid = native->st_gid;
    nbsd43->st_rdev = (uint16_t)native->st_rdev;
    nbsd43->st_size = (int32_t)native->st_size;
    nbsd43->st_atime = (int32_t)native->st_atime;
    nbsd43->st_mtime = (int32_t)native->st_mtime;
    nbsd43->st_ctime = (int32_t)native->st_ctime;
    nbsd43->st_blksize = (int32_t)native->st_blksize;
    nbsd43->st_blocks = (int32_t)native->st_blocks;
    nbsd43->st_flags = 0;
    nbsd43->st_gen = 0;
}

int netbsd_sys_stat(const char *path, struct netbsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_lstat(const char *path, struct netbsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_fstat(int fd, struct netbsd_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_compat_stat(const char *path, struct netbsd_stat43 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_compat_lstat(const char *path, struct netbsd_stat43 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_compat_fstat(int fd, struct netbsd_stat43 *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}
