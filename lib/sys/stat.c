/*
 * stat.c - stat()/lstat() syscall wrappers
 *
 * Provides typed wrappers for the STAT and LSTAT system calls.
 */
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

int stat(const char *path, struct stat *buf) {
    int ret = (int)syscall(SYS_STAT, path, buf);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

int lstat(const char *path, struct stat *buf) {
    int ret = (int)syscall(SYS_LSTAT, path, buf);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
