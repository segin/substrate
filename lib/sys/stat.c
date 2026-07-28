/*
 * stat.c - stat()/lstat() syscall wrappers
 *
 * Provides typed wrappers for the STAT and LSTAT system calls.
 */
#include <errno.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/syscall.h>

int sys_stat(const char *path, struct stat *buf) {
    int ret = (int)syscall(SYS_STAT, path, buf);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

int sys_lstat(const char *path, struct stat *buf) {
    int ret = (int)syscall(SYS_LSTAT, path, buf);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
