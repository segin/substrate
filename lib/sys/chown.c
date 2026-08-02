/*
 * chown.c - chown / lchown / chmod / lchmod / fchmod / fchown wrappers
 *
 * Substrate native syscall numbers follow V7 where possible, with BSD-era
 * additions at higher slots:
 *   16  chown   (V7 — follows symlinks per POSIX)
 *   15  chmod   (V7)
 *   94  fchmod
 *   95  fchown
 *   254 lchown  (BSD-era — does NOT follow symlinks)
 *   274 lchmod  (BSD-era — does NOT follow symlinks)
 *   297 fchmodat
 *   260 fchownat
 */

#include <errno.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

static int __set_errno(int rc) {
    if (rc < 0) { errno = -rc; return -1; }
    return rc;
}

int sys_chmod(const char *path, mode_t mode) {
    return __set_errno((int)syscall(SYS_CHMOD, path, (int)mode));
}

int sys_lchmod(const char *path, mode_t mode) {
    return __set_errno((int)syscall(SYS_LCHMOD, path, (int)mode));
}

int sys_fchmod(int fd, mode_t mode) {
    return __set_errno((int)syscall(SYS_FCHMOD, fd, (int)mode));
}

int sys_fchmodat(int dirfd, const char *path, mode_t mode, int flags) {
    return __set_errno((int)syscall(SYS_FCHMODAT, dirfd, path, (int)mode, flags));
}

int sys_chown(const char *path, uid_t owner, gid_t group) {
    return __set_errno((int)syscall(SYS_CHOWN, path, (int)owner, (int)group));
}

int sys_lchown(const char *path, uid_t owner, gid_t group) {
    return __set_errno((int)syscall(SYS_LCHOWN, path, (int)owner, (int)group));
}

int sys_fchown(int fd, uid_t owner, gid_t group) {
    return __set_errno((int)syscall(SYS_FCHOWN, fd, (int)owner, (int)group));
}

int sys_fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags) {
    return __set_errno((int)syscall(SYS_FCHOWNAT, dirfd, path, (int)owner, (int)group, flags));
}
