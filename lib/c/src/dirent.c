#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

extern int _syscall3(int, uintptr_t, uintptr_t, uintptr_t);

DIR *fdopendir(int fd) {
    if (fd < 0) {
        errno = EBADF;
        return NULL;
    }

    DIR *dir = malloc(sizeof(DIR));
    if (!dir) {
        close(fd);
        return NULL;
    }

    dir->fd = fd;
    dir->buf_pos = 0;
    dir->buf_end = 0;
    return dir;
}

DIR *opendir(const char *name) {
    int fd = open(name, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return NULL;

    return fdopendir(fd);
}

struct dirent *readdir(DIR *dirp) {
    struct dirent *d;
    size_t reclen;

    if (dirp == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (dirp->buf_pos >= dirp->buf_end) {
        // Refill buffer
        int ret = _syscall3(SYS_GETDENTS, dirp->fd, (uintptr_t)dirp->buf, sizeof(dirp->buf));
        if (ret <= 0) return NULL;
        dirp->buf_end = ret;
        dirp->buf_pos = 0;
    }

    if (dirp->buf_end - dirp->buf_pos < (int)offsetof(struct dirent, d_name)) {
        errno = EIO;
        return NULL;
    }

    d = (struct dirent *)(dirp->buf + dirp->buf_pos);
    reclen = d->d_reclen;
    if (reclen < offsetof(struct dirent, d_name) ||
        reclen > (size_t)(dirp->buf_end - dirp->buf_pos)) {
        errno = EIO;
        return NULL;
    }

    dirp->buf_pos += (int)reclen;
    return d;
}

/* POSIX readdir_r: read the next entry into the caller-owned `entry` buffer and
 * point *result at it (or NULL at end-of-directory).  Returns 0 on success or a
 * positive errno on error.  Like glibc's, this serializes against the shared
 * DIR buffer rather than supporting concurrent reads of the same stream; the
 * thread-safety it guarantees is that the returned struct is caller-owned.
 * (readdir_r is obsolescent but still required by code such as Motif's Xos_r.h
 * MT-safe path when _POSIX_THREAD_SAFE_FUNCTIONS is in effect.) */
int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result) {
    struct dirent *d;
    size_t copylen;

    if (dirp == NULL || entry == NULL || result == NULL)
        return EINVAL;

    errno = 0;
    d = readdir(dirp);
    if (d == NULL) {
        *result = NULL;
        return errno;              /* 0 at clean end-of-directory */
    }

    copylen = d->d_reclen;
    if (copylen > sizeof(struct dirent))
        copylen = sizeof(struct dirent);
    memcpy(entry, d, copylen);
    *result = entry;
    return 0;
}

int closedir(DIR *dirp) {
    close(dirp->fd);
    free(dirp);
    return 0;
}

int dirfd(DIR *dirp) {
    if (dirp == NULL) {
        errno = EINVAL;
        return -1;
    }
    return dirp->fd;
}
