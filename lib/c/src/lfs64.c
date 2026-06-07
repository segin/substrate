/*
 * lfs64.c — Large File Support (*64) entry points.
 *
 * substrate's off_t / struct stat / struct dirent / struct rlimit are already
 * natively 64-bit, so glibc's transitional "*64" symbols are exact aliases of
 * the base calls.  Code built with _LARGEFILE64_SOURCE (ksh93's libast,
 * sqlite, ...) references them directly; provide them as thin forwarders.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

int open64(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return open(path, flags, mode);
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return openat(dirfd, path, flags, mode);
}

int creat64(const char *path, mode_t mode)            { return creat(path, mode); }
off_t lseek64(int fd, off_t offset, int whence)       { return lseek(fd, offset, whence); }
int ftruncate64(int fd, off_t length)                 { return ftruncate(fd, length); }
int truncate64(const char *path, off_t length)        { return truncate(path, length); }
int stat64(const char *path, struct stat *st)         { return stat(path, st); }
int fstat64(int fd, struct stat *st)                  { return fstat(fd, st); }
int lstat64(const char *path, struct stat *st)        { return lstat(path, st); }
int fstatat64(int dfd, const char *p, struct stat *st, int f) { return fstatat(dfd, p, st, f); }
int statfs64(const char *path, struct statfs *b)      { return statfs(path, b); }
int fstatfs64(int fd, struct statfs *b)               { return fstatfs(fd, b); }
int statvfs64(const char *path, struct statvfs *b)    { return statvfs(path, b); }
int fstatvfs64(int fd, struct statvfs *b)             { return fstatvfs(fd, b); }
int getrlimit64(int res, struct rlimit *rl)           { return getrlimit(res, rl); }
int setrlimit64(int res, const struct rlimit *rl)     { return setrlimit(res, rl); }
struct dirent *readdir64(DIR *d)                      { return readdir(d); }
int readdir64_r(DIR *d, struct dirent *e, struct dirent **r) { return readdir_r(d, e, r); }

void *mmap64(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    return mmap(addr, len, prot, flags, fd, off);
}

int fcntl64(int fd, int cmd, ...)
{
    va_list ap;
    void *arg;
    va_start(ap, cmd);
    arg = va_arg(ap, void *);
    va_end(ap);
    return fcntl(fd, cmd, arg);
}
