#include "sunos_user.h"
#include <sys/kern_syscalls.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/errno.h>
#include <stddef.h>

/* Helper to translate native stat to SunOS stat */
static void translate_stat_to_sunos(const struct stat *native, struct sunos_stat *sunos) {
    memset(sunos, 0, sizeof(*sunos));
    sunos->st_dev = (uint16_t)native->st_dev;
    sunos->st_ino = (uint16_t)native->st_ino;
    sunos->st_mode = native->st_mode;
    sunos->st_nlink = native->st_nlink;
    sunos->st_uid = native->st_uid;
    sunos->st_gid = native->st_gid;
    sunos->st_rdev = (uint16_t)native->st_rdev;
    sunos->st_size = (int32_t)native->st_size;
    sunos->st_atime = (int32_t)native->st_atime;
    sunos->st_mtime = (int32_t)native->st_mtime;
    sunos->st_ctime = (int32_t)native->st_ctime;
    sunos->st_blksize = (int32_t)native->st_blksize;
    sunos->st_blocks = (int32_t)native->st_blocks;
}

int sunos_sys_stat(const char *path, struct sunos_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct sunos_stat ksunos;
        translate_stat_to_sunos(&native, &ksunos);
        if (copyout(&ksunos, buf, sizeof(ksunos)) != 0) return -14; // EFAULT
    }
    return ret;
}

int sunos_sys_lstat(const char *path, struct sunos_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct sunos_stat ksunos;
        translate_stat_to_sunos(&native, &ksunos);
        if (copyout(&ksunos, buf, sizeof(ksunos)) != 0) return -14; // EFAULT
    }
    return ret;
}

int sunos_sys_fstat(int fd, struct sunos_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct sunos_stat ksunos;
        translate_stat_to_sunos(&native, &ksunos);
        if (copyout(&ksunos, buf, sizeof(ksunos)) != 0) return -14; // EFAULT
    }
    return ret;
}
