#include "cp_atomic.h"

#include "cp_path.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int cp_atomic_fsync_fd(int fd)
{
#ifdef CP_HOST_BUILD
    while (fsync(fd) != 0) {
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
#else
    (void)fd;
#endif
    return 0;
}

static int cp_atomic_fsync_dir(const char *path)
{
#ifdef CP_HOST_BUILD
    int dfd;

#ifdef O_DIRECTORY
    dfd = open(path, O_RDONLY | O_DIRECTORY, 0);
#else
    dfd = open(path, O_RDONLY, 0);
#endif
    if (dfd < 0) {
        return -1;
    }

    if (cp_atomic_fsync_fd(dfd) != 0) {
        int saved = errno;
        close(dfd);
        errno = saved;
        return -1;
    }

    close(dfd);
#else
    (void)path;
#endif
    return 0;
}

int cp_atomic_open_temp(const char *dest_path, mode_t mode,
                        char **tmp_path_out, int *fd_out)
{
    char *dir;
    int fd = -1;
    int attempts;

    dir = cp_path_dirname(dest_path);
    if (!dir) {
        return -1;
    }

    for (attempts = 0; attempts < 1024; ++attempts) {
        unsigned long r = (unsigned long)arc4random();
        char name[32];
        char *candidate;

        snprintf(name, sizeof(name), ".cp.%06lx", r & 0xFFFFFFUL);
        candidate = cp_path_join(dir, name);
        if (!candidate) {
            free(dir);
            return -1;
        }

        fd = open(candidate, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC,
                  (int)(mode & 0777));
        if (fd >= 0) {
            *tmp_path_out = candidate;
            *fd_out = fd;
            free(dir);
            return 0;
        }

        if (errno != EEXIST) {
            free(candidate);
            free(dir);
            return -1;
        }

        free(candidate);
    }

    free(dir);
    errno = EEXIST;
    return -1;
}

int cp_atomic_commit(int fd, const char *tmp_path, const char *dest_path)
{
    char *dir;

    if (cp_atomic_fsync_fd(fd) != 0) {
        return -1;
    }

    if (close(fd) != 0) {
        return -1;
    }

    if (rename(tmp_path, dest_path) != 0) {
        return -1;
    }

    dir = cp_path_dirname(dest_path);
    if (!dir) {
        return -1;
    }

    (void)cp_atomic_fsync_dir(dir);
    free(dir);
    return 0;
}

void cp_atomic_cleanup(int fd, const char *tmp_path)
{
    if (fd >= 0) {
        close(fd);
    }
    if (tmp_path) {
        unlink(tmp_path);
    }
}
